#include "stdafx.h"
#include "FESlidingInterface2.h"
#include "fem.h"
#include "FESolidSolver.h"
#include "log.h"

//-----------------------------------------------------------------------------
// FEContactSurface2
//-----------------------------------------------------------------------------

FEContactSurface2::FEContactSurface2(FEM* pfem) : FESurface(&pfem->m_mesh)
{ 
	m_pfem = pfem; 
}

//-----------------------------------------------------------------------------
void FEContactSurface2::Init()
{
	// initialize surface data first
	FESurface::Init();

	// count how many integration points we have
	int nint = 0, i;
	for (i=0; i<Elements(); ++i)
	{
		FESurfaceElement& el = Element(i);
		nint += el.GaussPoints();
	}

	// allocate data structures
	m_gap.create(nint);
	m_nu.create(nint);
	m_rs.create(nint);
	m_Lmd.create(nint);
	m_Lmp.create(nint);
	m_pme.create(nint);

	m_nn.create(Nodes());
	m_nn.zero();

	m_nei.create(Elements());
	nint = 0;
	for (i=0; i<Elements(); ++i)
	{
		m_nei[i] = nint;
		nint += Element(i).GaussPoints();
	}

	// set intial values
	m_gap.zero();
	m_nu.zero();
	m_pme.set(0);
	m_Lmd.zero();
	m_Lmp.zero();

	// allocate biphasic stuff
	if (m_pfem->m_pStep->m_nModule == FE_POROELASTIC)
	{
		m_pg.create(nint);
		m_pg.zero();
	}
}

//-----------------------------------------------------------------------------
void FEContactSurface2::ShallowCopy(FEContactSurface2 &s)
{
	m_Lmd = s.m_Lmd;
	m_Lmp = s.m_Lmp;
	m_gap = s.m_gap;
	m_pme.zero();
}

//-----------------------------------------------------------------------------
//! This function calculates the node normal. Due to the piecewise continuity
//! of the surface elements this normal is not uniquely defined so in order to
//! obtain a unique normal the normal is averaged for each node over all the 
//! element normals at the node

void FEContactSurface2::UpdateNodeNormals()
{
	int N = Nodes(), i, j, ne, jp1, jm1;
	vec3d y[4], n;

	// zero nodal normals
	m_nn.zero();

	// loop over all elements
	for (i=0; i<Elements(); ++i)
	{
		FESurfaceElement& el = Element(i);
		ne = el.Nodes();

		// get the nodal coordinates
		for (j=0; j<ne; ++j) y[j] = Node(el.m_lnode[j]).m_rt;

		// calculate the normals
		for (j=0; j<ne; ++j)
		{
			jp1 = (j+1)%ne;
			jm1 = (j+ne-1)%ne;
			n = (y[jp1] - y[j]) ^ (y[jm1] - y[j]);
			m_nn[el.m_lnode[j]] += n;
		}
	}

	// normalize all vectors
	for (i=0; i<N; ++i) m_nn[i].unit();
}

//-----------------------------------------------------------------------------
// FESlidingInterface2
//-----------------------------------------------------------------------------

FESlidingInterface2::FESlidingInterface2(FEM* pfem) : FEContactInterface(pfem), m_ss(pfem), m_ms(pfem)
{
	m_ntype = FE_CONTACT_SLIDING2;
	static int count = 1;
	m_nID = count++;

	// initial values
	m_knmult = 1;
	m_atol = 0.02;
	m_eps = 1;
	m_epsp = 1;
	m_npass = 1;
	m_stol = 0.01;
	m_bsymm = true;
	m_srad = 0.1;

	m_naugmin = 0;
	m_naugmax = 10;
}

//-----------------------------------------------------------------------------
void FESlidingInterface2::Init()
{
	// initialize surface data
	m_ss.Init();
	m_ms.Init();

	bool bporo = m_pfem->m_pStep->m_nModule == FE_POROELASTIC;

	// get the logfile
	Logfile& log = GetLogfile();
	
	// this contact implementation requires a non-symmetric stiffness matrix
	// so inform the FEM class
	if (!m_bsymm) 
	{
		// request a non-symmetric stiffness matrix
		m_pfem->SetSymmetryFlag(false);

		// make sure we are using full-Newton
		if (bporo && (m_pfem->m_pStep->m_psolver->m_maxups != 0))
		{
			m_pfem->m_pStep->m_psolver->m_maxups = 0;
			log.printbox("WARNING", "The non-symmetric biphasic contact algorithm does not work with BFGS yet.\nThe full-Newton method will be used instead.");
		}
	}

	// update sliding interface data
	Update();
}

//-----------------------------------------------------------------------------
void FESlidingInterface2::ProjectSurface(FEContactSurface2& ss, FEContactSurface2& ms)
{
	FEMesh& mesh = m_pfem->m_mesh;
	FESurfaceElement* pme;
	vec3d r, nu;
	double rs[2];
	double Ln;

	bool bporo = (m_pfem->m_pStep->m_nModule == FE_POROELASTIC);

	double ps[4], p1;

	double R = m_srad*mesh.GetBoundingBox().radius();

	// loop over all integration points
	int n = 0;
	for (int i=0; i<ss.Elements(); ++i)
	{
		FESurfaceElement& el = ss.Element(i);
		mesh.UnpackElement(el);

		int ne = el.Nodes();
		int nint = el.GaussPoints();

		// get the nodal pressures
		if (bporo)
		{
			for (int j=0; j<ne; ++j) ps[j] = el.pt()[j];
		}

		for (int j=0; j<nint; ++j, ++n)
		{
			// calculate the global position of the integration point
			r = ss.Local2Global(el, j);

			// get the pressure at the integration point
			if (bporo) p1 = el.eval(ps, j);

			// calculate the normal at this integration point
			nu = ss.SurfaceNormal(el, j);

			// first see if the old intersected face is still good enough
			pme = ss.m_pme[n];
			if (pme)
			{
				double g;

				// see if the ray intersects this element
				if (ms.Intersect(*pme, r, nu, rs, g, m_stol))
				{
					ss.m_rs[n][0] = rs[0];
					ss.m_rs[n][1] = rs[1];
				}
				else pme = 0;
			}

			// find the intersection point with the master surface
			if (pme == 0) pme = ms.FindIntersection(r, nu, rs, m_stol);

			ss.m_pme[n] = pme;
			ss.m_nu[n] = nu;
			ss.m_rs[n][0] = rs[0];
			ss.m_rs[n][1] = rs[1];
			if (pme)
			{
				// the node could potentially be in contact
				// find the global location of the intersection point
				vec3d q = ms.Local2Global(*pme, rs[0], rs[1]);

				// calculate the gap function
				// NOTE: this has the opposite sign compared
				// to Gerard's notes.
				double g = nu*(r - q);
				Ln = ss.m_Lmd[n] + m_eps*g;

//				if ((g >= 0) && (g <= R))
				if ((Ln > 0) && (g <= R))
				{
					ss.m_gap[n] = g;

					// calculate the pressure gap function
					if (bporo) {
						mesh.UnpackElement(*pme);
						double p2 = pme->eval(pme->pt(), rs[0], rs[1]);
						ss.m_pg[n] = p1 - p2;
					}
				}
				else
				{
					ss.m_Lmd[n] = 0;
					ss.m_gap[n] = 0;
					ss.m_pme[n] = 0;
					if (bporo) {
						ss.m_Lmp[n] = 0;
						ss.m_pg[n] = 0;
					}
				}
			}
			else
			{
				// the node is not in contact
				ss.m_Lmd[n] = 0;
				ss.m_gap[n] = 0;
				if (bporo) {
					ss.m_Lmp[n] = 0;
					ss.m_pg[n] = 0;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FESlidingInterface2::Update()
{	
	int i, j, n, id;

	// project the surfaces onto each other
	// this will update the gap functions as well
	ProjectSurface(m_ss, m_ms);
	if (m_npass == 2) ProjectSurface(m_ms, m_ss);

	// set poro flag
	bool bporo = (m_pfem->m_pStep->m_nModule == FE_POROELASTIC);

	// only continue if we are doing a poro-elastic simulation
	if (bporo == false) return;

	// now that the nodes have been projected, we need to figure out
	// if we need to modify the constraints on the pressure dofs
	for (int np=0; np<m_npass; ++np)
	{
		FEContactSurface2& ss = (np == 0? m_ss : m_ms);

		// first, mark all nodes as free-draining (= neg. ID)
		// this is done by setting the dof's equation number
		// to a positive number
		for (i=0; i<ss.Nodes(); ++i) 
		{
			id = ss.Node(i).m_ID[6];
			if (id >= 0) {
				FENode& node = ss.Node(i);
				// mark node as free-draining
				node.m_ID[6] = -id-2;
			}
		}
		
		// keep a running counter of integration points.
		int ni = 0;

		// loop over all elements
		for (n=0; n<ss.Elements(); ++n)
		{
			FESurfaceElement& el = ss.Element(n);

			// get the nodal tractions at the integration points
			double ti[4], gi[4];
			int nint = el.GaussPoints();
			int neln = el.Nodes();
			for (i=0; i<nint; ++i, ++ni) 
			{
				gi[i] = ss.m_gap[ni];
				ti[i] = MBRACKET(ss.m_Lmd[ni] + m_eps*gi[i]);
			}

			// setup the (over-determined) system to find the nodal values
			// TODO: this is the same for all surface elements, so
			//       maybe we should do this only once
			matrix A;
			A.Create(nint, neln);
			for (i=0; i<nint; ++i)
			{
				double* H = el.H(i);
				for (j=0; j<neln; ++j) A[i][j] = H[j];
			}

			double tn[4];

			if (nint == neln)
			{
				matrix Ai = A.inverse();

				for (i=0; i<neln; ++i)
				{
					tn[i] = 0;
					for (j=0; j<nint; ++j) tn[i] += Ai[i][j]*ti[j];
				}
			}
			else
			{
				matrix At = A.transpose();
				matrix C = At*A;
				matrix D = C.inverse()*At;

				for (i=0; i<neln; ++i)
				{
					tn[i] = 0;
					for (j=0; j<nint; ++j) tn[i] += D[i][j]*ti[j];
				}
			}

			for (i=0; i<neln; ++i)
			{
				FENode& node = ss.Node(el.m_lnode[i]);
				id = node.m_ID[6];
				if ((id < -1) && (tn[i] > 0))
				{
					// mark node as non free-draining
					node.m_ID[6] = -id-2;
				}
			}
		}
		
		// now set the pressure of free-draining nodes to zero
		for (i=0; i<ss.Nodes(); ++i) 
		{
			if (ss.Node(i).m_ID[6] < -1) {
				FENode& node = ss.Node(i);
				// set the fluid pressure to zero
				node.m_pt = 0;
			}
		}
	}

	// if we only did single pass, the dofs of the secondary surface 
	// have not been modified, so we modify them here.
	if (m_npass == 1)
	{
		// we need to figure out if we need to fix the pressure dof
		// for the nodes on the secondary surface. We do this by
		// finding the traction value on the projection point on the 
		// primary surface.

		// since we'll do a projection in the direction of the local
		// normal, we first calculate these nodal normals
		m_ms.UpdateNodeNormals();

		double rs[2];

		// loop over all nodes of the secondary surface
		for (int n=0; n<m_ms.Nodes(); ++n)
		{
			// get the node
			FENode& node = m_ms.Node(n);
			id = node.m_ID[6];
			if (id >= 0) {
				// mark node as free-draining
				node.m_ID[6] = -id-2;
			}
			
			// project it onto the primary surface
			int nei;
			FESurfaceElement* pse = m_ss.FindIntersection(node.m_rt, m_ms.m_nn[n], rs, m_stol, &nei);

			if (pse)
			{
				// we found an element so let's calculate the nodal traction values for this element
	
				// get the nodal tractions at the integration points
				double ti[4], gi[4];
				int nint = pse->GaussPoints();
				int neln = pse->Nodes();
				int noff = m_ss.m_nei[nei];
				for (i=0; i<nint; ++i) 
				{
					gi[i] = m_ss.m_gap[noff + i];
					ti[i] = MBRACKET(m_ss.m_Lmd[noff + i] + m_eps*gi[i]);
				}

				// setup the (over-determined) system to find the nodal values
				// TODO: this is the same for all surface elements, so
				//       maybe we should do this only once
				matrix A;
				A.Create(nint, neln);
				for (i=0; i<nint; ++i)
				{
					double* H = pse->H(i);
					for (j=0; j<neln; ++j) A[i][j] = H[j];
				}

				double tn[4];

				if (nint == neln)
				{
					matrix Ai = A.inverse();

					for (i=0; i<neln; ++i)
					{
						tn[i] = 0;
						for (j=0; j<nint; ++j) tn[i] += Ai[i][j]*ti[j];
					}
				}
				else
				{
					matrix At = A.transpose();
					matrix C = At*A;
					matrix D = C.inverse()*At;

					for (i=0; i<neln; ++i)
					{
						tn[i] = 0;
						for (j=0; j<nint; ++j) tn[i] += D[i][j]*ti[j];
					}
				}

				// now evaluate the traction at the intersection point
				double tp = pse->eval(tn, rs[0], rs[1]);

				// if tp > 0, mark node as non free-draining.
				id = node.m_ID[6];
				if ((id < -1) && (tp > 0))
				{
					// mark as non free-draining
					node.m_ID[6] = -id-2;
				}
			}
		}
		
		// loop over all nodes of the secondary surface 
		// and set pressure to zero for free-draining nodes
		for (int n=0; n<m_ms.Nodes(); ++n)
		{
			FENode& node = m_ms.Node(n);
			if (node.m_ID[6] < -1)
			{
				// set the fluid pressure to zero
				node.m_pt = 0;
			}			
		}
	}
}

//-----------------------------------------------------------------------------
void FESlidingInterface2::ShallowCopy(FEContactInterface &ci)
{
	FESlidingInterface2& si = dynamic_cast<FESlidingInterface2&>(ci);
	m_ss.ShallowCopy(si.m_ss);
	m_ms.ShallowCopy(si.m_ms);
}

//-----------------------------------------------------------------------------
void FESlidingInterface2::ContactForces(vector<double> &F)
{
	int i, j, k;
	vector<int> sLM, mLM, LM, en;
	vector<double> fe;
	double detJ[4], w[4], *Hs, Hm[4];

	// get the mesh
	FEMesh* pm = m_ss.GetMesh();

	// get the solver
	FESolidSolver* psolver = dynamic_cast<FESolidSolver*>(m_pfem->m_pStep->m_psolver);

	// set poro flag
	bool bporo = (m_pfem->m_pStep->m_nModule == FE_POROELASTIC);
	
	// get the poro-elasticity symmetry flag
	bool bsymm = m_pfem->m_bsym_poro;

	// if we're using the symmetric formulation
	// we need to multiply with the timestep
	double dt = (bsymm?m_pfem->m_pStep->m_dt:1);

	// loop over the nr of passes
	for (int np=0; np<m_npass; ++np)
	{
		// get slave and master surface
		FEContactSurface2& ss = (np == 0? m_ss : m_ms);
		FEContactSurface2& ms = (np == 0? m_ms : m_ss);

		// keep a running counter of integration points
		int ni = 0;

		// loop over all slave elements
		for (i=0; i<ss.Elements(); ++i)
		{
			// get the surface element
			FESurfaceElement& se = ss.Element(i);
			pm->UnpackElement(se);

			// get the nr of nodes and integration points
			int nseln = se.Nodes();
			int nint = se.GaussPoints();

			// copy the LM vector; we'll need it later
			sLM = se.LM();

			// we calculate all the metrics we need before we
			// calculate the nodal forces
			for (j=0; j<nint; ++j)
			{
				// get the base vectors
				vec3d g[2];
				ss.CoBaseVectors(se, j, g);

				// jacobians: J = |g0xg1|
				detJ[j] = (g[0] ^ g[1]).norm();

				// integration weights
				w[j] = se.GaussWeights()[j];
			}

			// loop over all integration points
			// note that we are integrating over the current surface
			for (j=0; j<nint; ++j, ++ni)
			{
				// get the master element
				FESurfaceElement* pme = ss.m_pme[ni];
				if (pme)
				{
					// get the master element
					FESurfaceElement& me = *pme;
					pm->UnpackElement(me);

					// get the nr of master element nodes
					int nmeln = me.Nodes();

					// copy LM vector
					mLM = me.LM();

					// calculate degrees of freedom
					int ndof = 3*(nseln + nmeln);

					// build the LM vector
					LM.create(ndof);
					for (k=0; k<nseln; ++k)
					{
						LM[3*k  ] = sLM[3*k  ];
						LM[3*k+1] = sLM[3*k+1];
						LM[3*k+2] = sLM[3*k+2];
					}

					for (k=0; k<nmeln; ++k)
					{
						LM[3*(k+nseln)  ] = mLM[3*k  ];
						LM[3*(k+nseln)+1] = mLM[3*k+1];
						LM[3*(k+nseln)+2] = mLM[3*k+2];
					}

					// build the en vector
					en.create(nseln+nmeln);
					for (k=0; k<nseln; ++k) en[k      ] = se.m_node[k];
					for (k=0; k<nmeln; ++k) en[k+nseln] = me.m_node[k];

					// get slave element shape functions
					Hs = se.H(j);

					// get master element shape functions
					double r = ss.m_rs[ni][0];
					double s = ss.m_rs[ni][1];
					me.shape_fnc(Hm, r, s);

					// get normal vector
					vec3d nu = ss.m_nu[ni];

					// gap function
					double g = ss.m_gap[ni];
					
					// lagrange multiplier
					double Lm = ss.m_Lmd[ni];

					// contact traction
					double tn = Lm + m_eps*g;
					tn = MBRACKET(tn);

					// calculate the force vector
					fe.create(ndof);

					for (k=0; k<nseln; ++k)
					{
						fe[3*k  ] = -Hs[k]*nu.x;
						fe[3*k+1] = -Hs[k]*nu.y;
						fe[3*k+2] = -Hs[k]*nu.z;
					}

					for (k=0; k<nmeln; ++k)
					{
						fe[3*(k+nseln)  ] = Hm[k]*nu.x;
						fe[3*(k+nseln)+1] = Hm[k]*nu.y;
						fe[3*(k+nseln)+2] = Hm[k]*nu.z;
					}

					for (k=0; k<ndof; ++k) fe[k] *= tn*detJ[j]*w[j];

					// assemble the global residual
					psolver->AssembleResidual(en, LM, fe, F);

					// do the biphasic stuff
					// TODO: I should only do this when the node is actually in contact
					//       in other words, when g >= 0
					if (bporo && (tn > 0))
					{
						// calculate nr of pressure dofs
						int ndof = nseln + nmeln;

						// calculate the flow rate
						double wn = ss.m_Lmp[ni] + m_epsp*ss.m_pg[ni];

						// fill the LM
						LM.create(ndof);
						for (k=0; k<nseln; ++k) LM[k        ] = sLM[3*nseln+k];
						for (k=0; k<nmeln; ++k) LM[k + nseln] = mLM[3*nmeln+k];

						// fill the force array
						fe.create(ndof);
						for (k=0; k<nseln; ++k) fe[k      ] =  Hs[k];
						for (k=0; k<nmeln; ++k) fe[k+nseln] = -Hm[k];

						// NOTE: note that dt is either the timestep
						//       or one, depending on whether we are 
						//       using the symmetric poro version or not
						for (k=0; k<ndof; ++k) fe[k] *= dt*wn*detJ[j]*w[j];

						// assemble residual
						psolver->AssembleResidual(en, LM, fe, F);
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FESlidingInterface2::ContactStiffness()
{
	int i, j, k, l;
	vector<int> sLM, mLM, LM, en;
	double detJ[4], w[4], *Hs, Hm[4], pt[4], dpr[4], dps[4];
	double N[24];
	matrix ke;

	// set the poro flag
	bool bporo = (m_pfem->m_pStep->m_nModule == FE_POROELASTIC);

	// get the poroelasticity symmetry flag
	bool bsymm = m_pfem->m_bsym_poro;

	// get the mesh
	FEMesh* pm = m_ss.GetMesh();

	// get the solver
	FESolidSolver* psolver = dynamic_cast<FESolidSolver*>(m_pfem->m_pStep->m_psolver);

	// see how many reformations we've had to do so far
	int nref = psolver->m_nref;

	// get the logfile
	Logfile& log = GetLogfile();
	
	// set higher order stiffness mutliplier
	double knmult = m_knmult;
	if (m_knmult < 0)
	{
		int ni = int(-m_knmult);
		if (nref >= ni)
		{
			knmult = 1; 
			log.printf("Higher order stiffness terms included.\n");
		}
		else knmult = 0;
	}

	// do single- or two-pass
	for (int np=0; np < m_npass; ++np)
	{
		// get the slave and master surface
		FEContactSurface2& ss = (np == 0? m_ss : m_ms);
		FEContactSurface2& ms = (np == 0? m_ms : m_ss);

		// keep a running counter of the integration points
		int ni = 0;

		// loop over all slave elements
		for (i=0; i<ss.Elements(); ++i)
		{
			// get ths slave element
			FESurfaceElement& se = ss.Element(i);
			pm->UnpackElement(se);

			// get nr of nodes and integration points
			int nseln = se.Nodes();
			int nint = se.GaussPoints();

			// copy the LM vector
			sLM = se.LM();

			// we calculate all the metrics we need before we
			// calculate the nodal forces
			for (j=0; j<nint; ++j)
			{
				// get the base vectors
				vec3d g[2];
				ss.CoBaseVectors(se, j, g);

				// jacobians: J = |g0xg1|
				detJ[j] = (g[0] ^ g[1]).norm();

				// integration weights
				w[j] = se.GaussWeights()[j];

				// pressure
				if (bporo)
				{
					pt[j] = se.eval(se.pt(), j);
					dpr[j] = se.eval_deriv1(se.pt(), j);
					dps[j] = se.eval_deriv2(se.pt(), j);
				}
			}

			// loop over all integration points
			for (j=0; j<nint; ++j, ++ni)
			{
				// get the master element
				FESurfaceElement* pme = ss.m_pme[ni];
				if (pme)
				{
					// --- S O L I D - S O L I D   C O N T A C T ---

					FESurfaceElement& me = *pme;
					pm->UnpackElement(me);

					// get the nr of master nodes
					int nmeln = me.Nodes();

					// copy the LM vector
					mLM = me.LM();

					// calculate degrees of freedom
					int ndof = 3*(nseln + nmeln);

					// build the LM vector
					LM.create(ndof);
					for (k=0; k<nseln; ++k)
					{
						LM[3*k  ] = sLM[3*k  ];
						LM[3*k+1] = sLM[3*k+1];
						LM[3*k+2] = sLM[3*k+2];
					}

					for (k=0; k<nmeln; ++k)
					{
						LM[3*(k+nseln)  ] = mLM[3*k  ];
						LM[3*(k+nseln)+1] = mLM[3*k+1];
						LM[3*(k+nseln)+2] = mLM[3*k+2];
					}

					// build the en vector
					en.create(nseln+nmeln);
					for (k=0; k<nseln; ++k) en[k      ] = se.m_node[k];
					for (k=0; k<nmeln; ++k) en[k+nseln] = me.m_node[k];

					// slave shape functions
					Hs = se.H(j);

					// master shape functions
					double r = ss.m_rs[ni][0];
					double s = ss.m_rs[ni][1];
					me.shape_fnc(Hm, r, s);

					// get slave normal vector
					vec3d nu = ss.m_nu[ni];

					// gap function
					double g = ss.m_gap[ni];
					
					// lagrange multiplier
					double Lm = ss.m_Lmd[ni];

					// contact traction
					double tn = Lm + m_eps*g;
					tn = MBRACKET(tn);

					double dtn = m_eps*HEAVYSIDE(Lm + m_eps*g);

					// a. NxN-term
					//------------------------------------

					// calculate the N-vector
					for (k=0; k<nseln; ++k)
					{
						N[3*k  ] = -Hs[k]*nu.x;
						N[3*k+1] = -Hs[k]*nu.y;
						N[3*k+2] = -Hs[k]*nu.z;
					}

					for (k=0; k<nmeln; ++k)
					{
						N[3*(k+nseln)  ] = Hm[k]*nu.x;
						N[3*(k+nseln)+1] = Hm[k]*nu.y;
						N[3*(k+nseln)+2] = Hm[k]*nu.z;
					}

					// create the stiffness matrix
					ke.Create(ndof, ndof);

					for (k=0; k<ndof; ++k)
						for (l=0; l<ndof; ++l) ke[k][l] = dtn*N[k]*N[l]*detJ[j]*w[j];

					// b. A-term
					//-------------------------------------

					for (k=0; k<nseln; ++k) N[k      ] =  Hs[k];
					for (k=0; k<nmeln; ++k) N[k+nseln] = -Hm[k];

					double* Gr = se.Gr(j);
					double* Gs = se.Gs(j);
					vec3d gs[2];
					ss.CoBaseVectors(se, j, gs);

					mat3d S1, S2;
					S1.skew(gs[0]);
					S2.skew(gs[1]);
					mat3d As[4];
					
					if (!m_bsymm)
					{
						for (l=0; l<nseln; ++l) {
							As[l] = S2*Gr[l] - S1*Gs[l];
							for (k=0; k<nseln+nmeln; ++k)
							{
								ke[k*3  ][l*3  ] -= tn*w[j]*N[k]*As[l][0][0];
								ke[k*3  ][l*3+1] -= tn*w[j]*N[k]*As[l][0][1];
								ke[k*3  ][l*3+2] -= tn*w[j]*N[k]*As[l][0][2];
								
								ke[k*3+1][l*3  ] -= tn*w[j]*N[k]*As[l][1][0];
								ke[k*3+1][l*3+1] -= tn*w[j]*N[k]*As[l][1][1];
								ke[k*3+1][l*3+2] -= tn*w[j]*N[k]*As[l][1][2];
								
								ke[k*3+2][l*3  ] -= tn*w[j]*N[k]*As[l][2][0];
								ke[k*3+2][l*3+1] -= tn*w[j]*N[k]*As[l][2][1];
								ke[k*3+2][l*3+2] -= tn*w[j]*N[k]*As[l][2][2];
							}
						}
					}
					// c. M-term
					//---------------------------------------

					vec3d Gm[2];
					ms.ContraBaseVectors0(me, r, s, Gm);
					
					// evaluate master surface normal
					vec3d mnu = Gm[0] ^ Gm[1];
					mnu.unit();

					double Hmr[4], Hms[4];
					me.shape_deriv(Hmr, Hms, r, s);
					vec3d mm[4];

					if (!m_bsymm)
					{
						for (k=0; k<nmeln; ++k) {
							mm[k] = Gm[0]*Hmr[k] + Gm[1]*Hms[k];
							for (l=0; l<nseln+nmeln; ++l)
							{
								ke[(k+nseln)*3  ][l*3  ] += tn*detJ[j]*w[j]*mnu.x*mm[k].x*N[l];
								ke[(k+nseln)*3  ][l*3+1] += tn*detJ[j]*w[j]*mnu.x*mm[k].y*N[l];
								ke[(k+nseln)*3  ][l*3+2] += tn*detJ[j]*w[j]*mnu.x*mm[k].z*N[l];
								
								ke[(k+nseln)*3+1][l*3  ] += tn*detJ[j]*w[j]*mnu.y*mm[k].x*N[l];
								ke[(k+nseln)*3+1][l*3+1] += tn*detJ[j]*w[j]*mnu.y*mm[k].y*N[l];
								ke[(k+nseln)*3+1][l*3+2] += tn*detJ[j]*w[j]*mnu.y*mm[k].z*N[l];
								
								ke[(k+nseln)*3+2][l*3  ] += tn*detJ[j]*w[j]*mnu.z*mm[k].x*N[l];
								ke[(k+nseln)*3+2][l*3+1] += tn*detJ[j]*w[j]*mnu.z*mm[k].y*N[l];
								ke[(k+nseln)*3+2][l*3+2] += tn*detJ[j]*w[j]*mnu.z*mm[k].z*N[l];
							}
						}
					}

					// assemble the global stiffness
					psolver->AssembleStiffness(en, LM, ke);

					// --- B I P H A S I C   S T I F F N E S S ---
					if (bporo && (tn > 0))
					{
						// the variable dt is either the timestep or one
						// depending on whether we are using the symmetric
						// poro version or not.
						double dt = (bsymm?m_pfem->m_pStep->m_dt:1);

						// --- S O L I D - P R E S S U R E   C O N T A C T ---

						int ndof = 4*(nseln+nmeln);
						LM.create(ndof);
						for (k=0; k<nseln; ++k)
						{
							LM[4*k  ] = sLM[3*k  ];			// x-dof
							LM[4*k+1] = sLM[3*k+1];			// y-dof
							LM[4*k+2] = sLM[3*k+2];			// z-dof
							LM[4*k+3] = sLM[3*nseln+k];		// p-dof
						}
						for (k=0; k<nmeln; ++k)
						{
							LM[4*(k+nseln)  ] = mLM[3*k  ];			// x-dof
							LM[4*(k+nseln)+1] = mLM[3*k+1];			// y-dof
							LM[4*(k+nseln)+2] = mLM[3*k+2];			// z-dof
							LM[4*(k+nseln)+3] = mLM[3*nmeln+k];		// p-dof
						}

						for (k=0; k<nseln; ++k) N[k      ] =  Hs[k];
						for (k=0; k<nmeln; ++k) N[k+nseln] = -Hm[k];

						ke.Create(ndof, ndof);
						ke.zero();

						// a. q-term
						//-------------------------------------
						
						double dpmr, dpms;
						dpmr = me.eval_deriv1(me.pt(), r, s);
						dpms = me.eval_deriv2(me.pt(), r, s);

						for (k=0; k<nseln+nmeln; ++k)
							for (l=0; l<nseln+nmeln; ++l)
							{
								ke[4*k + 3][4*l  ] += dt*w[j]*detJ[j]*m_epsp*N[k]*N[l]*(dpmr*Gm[0].x + dpms*Gm[1].x);
								ke[4*k + 3][4*l+1] += dt*w[j]*detJ[j]*m_epsp*N[k]*N[l]*(dpmr*Gm[0].y + dpms*Gm[1].y);
								ke[4*k + 3][4*l+2] += dt*w[j]*detJ[j]*m_epsp*N[k]*N[l]*(dpmr*Gm[0].z + dpms*Gm[1].z);
							}

						double wn = ss.m_Lmp[ni] + m_epsp*ss.m_pg[ni];

						if (!m_bsymm)
						{
							// b. A-term
							//-------------------------------------
							
							for (l=0; l<nseln; ++l)
								for (k=0; k<nseln+nmeln; ++k)
								{
									ke[4*k + 3][4*l  ] -= dt*w[j]*wn*N[k]*(As[l][0][0]*nu.x + As[l][0][1]*nu.y + As[l][0][2]*nu.z);
									ke[4*k + 3][4*l+1] -= dt*w[j]*wn*N[k]*(As[l][1][0]*nu.x + As[l][1][1]*nu.y + As[l][1][2]*nu.z);
									ke[4*k + 3][4*l+2] -= dt*w[j]*wn*N[k]*(As[l][2][0]*nu.x + As[l][2][1]*nu.y + As[l][2][2]*nu.z);
								}
	
							// c. m-term
							//---------------------------------------
								
							for (k=0; k<nmeln; ++k)
								for (l=0; l<nseln+nmeln; ++l)
								{
									ke[4*k + 3][4*l  ] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].x;
									ke[4*k + 3][4*l+1] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].y;
									ke[4*k + 3][4*l+2] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].z;
								}
							
							psolver->AssembleStiffness(en, LM, ke);
						}


						// --- P R E S S U R E - P R E S S U R E   C O N T A C T ---

						ndof = nseln+nmeln;

						for (k=0; k<nseln; ++k) N[k      ] =  Hs[k];
						for (k=0; k<nmeln; ++k) N[k+nseln] = -Hm[k];

						LM.create(ndof);
						for (k=0; k<nseln; ++k) LM[k      ] = sLM[3*nseln+k];
						for (k=0; k<nmeln; ++k) LM[k+nseln] = mLM[3*nmeln+k];

						// build the "element" stiffness
						ke.Create(ndof, ndof);
						for (k=0; k<ndof; ++k)
							for (l=0; l<ndof; ++l) ke[k][l] = -dt*m_epsp*w[j]*detJ[j]*N[k]*N[l];

						// assemble the global stiffness
						psolver->AssembleStiffness(en, LM, ke);
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
bool FESlidingInterface2::Augment(int naug)
{
	// make sure we need to augment
	if (!m_blaugon) return true;

	int i;
	double Ln, Lp;
	bool bconv = true;

	bool bporo = (m_pfem->m_pStep->m_nModule == FE_POROELASTIC);

	// --- c a l c u l a t e   i n i t i a l   n o r m s ---
	// a. normal component
	int NS = m_ss.m_Lmd.size();
	int NM = m_ms.m_Lmd.size();
//	bool contact = false;
	double maxgap = 0;
	double maxpg = 0;

	// update Lagrange multipliers
	for (i=0; i<NS; ++i)
	{
		// update Lagrange multipliers on slave surface
		Ln = m_ss.m_Lmd[i] + m_eps*m_ss.m_gap[i];
		m_ss.m_Lmd[i] = MBRACKET(Ln);
		
		if (bporo) {
			Lp = 0;
			if (Ln > 0) {
				Lp = m_ss.m_Lmp[i] + m_epsp*m_ss.m_pg[i];
				maxpg = max(maxpg,fabs(m_ss.m_pg[i]));
			}
			m_ss.m_Lmp[i] = Lp;
		}
		
		if (Ln > 0) {
			maxgap = max(maxgap,m_ss.m_gap[i]);
/*			if (contact)
				maxgap = max(maxgap,m_ss.m_gap[i]);
//				maxgap = max(maxgap,fabs(m_ss.m_gap[i]));
			else {
				contact = true;
				maxgap = m_ss.m_gap[i];
//				maxgap = fabs(m_ss.m_gap[i]);
			} */
		}
	}	
	
	for (i=0; i<NM; ++i)
	{
		// update Lagrange multipliers on master surface
		Ln = m_ms.m_Lmd[i] + m_eps*m_ms.m_gap[i];
		m_ms.m_Lmd[i] = MBRACKET(Ln);
		
		if (bporo) {
			Lp = 0;
			if (Ln > 0) {
				Lp = m_ms.m_Lmp[i] + m_epsp*m_ms.m_pg[i];
				maxpg = max(maxpg,fabs(m_ms.m_pg[i]));
			}
			m_ms.m_Lmp[i] = Lp;
		}
		
		if (Ln > 0) {
			maxgap = max(maxgap,m_ms.m_gap[i]);
/*			if (contact)
				maxgap = max(maxgap,m_ms.m_gap[i]);
//				maxgap = max(maxgap,fabs(m_ms.m_gap[i]));
			else {
				contact = true;
				maxgap = m_ms.m_gap[i];
//				maxgap = fabs(m_ms.m_gap[i]);
			} */
		}
	}

	// check convergence
	if (maxgap > m_gtol)
		bconv = false;
	if (bporo && maxpg > m_ptol)
		bconv = false;
	Logfile& log = GetLogfile();
	
	log.printf(" sliding interface # %d\n", m_nID);
	log.printf("                        CURRENT        REQUIRED\n");
	log.printf("    maximum gap  : %15le", maxgap);
	if (m_gtol > 0) log.printf("%15le\n", m_gtol); else log.printf("       ***\n");
	if (bporo) {
		log.printf("    maximum pgap : %15le", maxpg);
		if (m_ptol > 0) log.printf("%15le\n", m_ptol); else log.printf("       ***\n");
	}


	return bconv;
}

//-----------------------------------------------------------------------------
void FESlidingInterface2::Serialize(Archive &ar)
{

}
