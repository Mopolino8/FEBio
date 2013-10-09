#include "FEMultiphasicSolver.h"
#include "FEMultiphasicDomain.h"
#include "FESlidingInterface2.h"
#include "FESlidingInterface3.h"
#include "FEBioMech/FEPressureLoad.h"
#include "FEBioMech/FERigidBody.h"
#include "FEBioMech/FEResidualVector.h"
#include "FECore/log.h"

#ifdef WIN32
	#include <float.h>
	#define ISNAN(x) _isnan(x)
#endif

#ifdef LINUX
	#include <math.h>
	#define ISNAN(x) isnan(x)
#endif

#ifdef __APPLE__
#include <math.h>
#define ISNAN(x) isnan(x)
#endif

//-----------------------------------------------------------------------------
// define the parameter list
BEGIN_PARAMETER_LIST(FEMultiphasicSolver, FESolver)
	ADD_PARAMETER(m_Dtol         , FE_PARAM_DOUBLE, "dtol"        );
	ADD_PARAMETER(m_Etol         , FE_PARAM_DOUBLE, "etol"        );
	ADD_PARAMETER(m_Rtol         , FE_PARAM_DOUBLE, "rtol"        );
	ADD_PARAMETER(m_Ptol         , FE_PARAM_DOUBLE, "ptol"        );
	ADD_PARAMETER(m_Ctol         , FE_PARAM_DOUBLE, "ctol"        );
	ADD_PARAMETER(m_Rmin         , FE_PARAM_DOUBLE, "min_residual");
	ADD_PARAMETER(m_bsymm        , FE_PARAM_BOOL  , "symmetric_biphasic");
	ADD_PARAMETER(m_bfgs.m_LStol , FE_PARAM_DOUBLE, "lstol"       );
	ADD_PARAMETER(m_bfgs.m_LSmin , FE_PARAM_DOUBLE, "lsmin"       );
	ADD_PARAMETER(m_bfgs.m_LSiter, FE_PARAM_INT   , "lsiter"      );
	ADD_PARAMETER(m_bfgs.m_maxref, FE_PARAM_INT   , "max_refs"    );
	ADD_PARAMETER(m_bfgs.m_maxups, FE_PARAM_INT   , "max_ups"     );
	ADD_PARAMETER(m_bfgs.m_cmax  , FE_PARAM_DOUBLE, "cmax"        );
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
FEMultiphasicSolver::FEMultiphasicSolver(FEModel& fem) : FESolidSolver(fem)
{
	m_Ctol = 0.01;
	for (int k=0; k<MAX_CDOFS; ++k) m_nceq[k] = 0;
}

//-----------------------------------------------------------------------------
//! Allocates and initializes the data structures.
//
bool FEMultiphasicSolver::Init()
{
	// initialize base class
	if (FESolidSolver::Init() == false) return false;

	// allocate poro-vectors
	assert(m_ndeq > 0);
	m_di.assign(m_ndeq, 0);
	m_Di.assign(m_ndeq, 0);

//	assert(m_npeq > 0);
	if (m_npeq > 0) {
		m_pi.assign(m_npeq, 0);
		m_Pi.assign(m_npeq, 0);

		// we need to fill the total displacement vector m_Ut
		// TODO: I need to find an easier way to do this
		FEMesh& mesh = m_fem.GetMesh();
		for (int i=0; i<mesh.Nodes(); ++i)
		{
			FENode& node = mesh.Node(i);

			// pressure dofs
			int n = node.m_ID[DOF_P]; if (n >= 0) m_Ut[n] = node.m_pt;
		}
	}

	// allocate concentration-vectors
	m_ci.assign(MAX_CDOFS,vector<double>(0,0));
	m_Ci.assign(MAX_CDOFS,vector<double>(0,0));
	for (int i=0; i<MAX_CDOFS; ++i) {
		m_ci[i].assign(m_nceq[i], 0);
		m_Ci[i].assign(m_nceq[i], 0);
	}
	
	// we need to fill the total displacement vector m_Ut
	// TODO: I need to find an easier way to do this
	FEMesh& mesh = m_fem.GetMesh();
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		
		// concentration dofs
		for (int j=0; j<MAX_CDOFS; ++j) {
			if (m_nceq[j]) {
				int n = node.m_ID[DOF_C+j];
				if (n >= 0) m_Ut[n] = node.m_ct[j];
			}
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
//! Initialize equations
bool FEMultiphasicSolver::InitEquations()
{
	// base class does most of the work
	FESolidSolver::InitEquations();

	// determined the nr of pressure and concentration equations
	FEMesh& mesh = m_fem.GetMesh();
	m_ndeq = m_npeq = 0;
	
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& n = mesh.Node(i);
		if (n.m_ID[DOF_X] != -1) m_ndeq++;
		if (n.m_ID[DOF_Y] != -1) m_ndeq++;
		if (n.m_ID[DOF_Z] != -1) m_ndeq++;
		if (n.m_ID[DOF_P] != -1) m_npeq++;
	}
	
	// determined the nr of concentration equations
	for (int j=0; j<MAX_CDOFS; ++j) m_nceq[j] = 0;
	
	for (int i=0; i<mesh.Nodes(); ++i)
	{
		FENode& n = mesh.Node(i);
		for (int j=0; j<MAX_CDOFS; ++j)
			if (n.m_ID[DOF_C+j] != -1) m_nceq[j]++;
	}
	
	return true;
}

//-----------------------------------------------------------------------------
//! Prepares the data for the first QN iteration. 
//!
void FEMultiphasicSolver::PrepStep(double time)
{
	for (int j=0; j<MAX_CDOFS; ++j)
		if (m_nceq[j]) zero(m_Ci[j]);

	zero(m_Pi);
	zero(m_Di);

	FESolidSolver::PrepStep(time);
}

//-----------------------------------------------------------------------------
//! Implements the BFGS algorithm to solve the nonlinear FE equations.
//! The details of this implementation of the BFGS method can be found in:
//!   "Finite Element Procedures", K.J. Bathe, p759 and following
//!
bool FEMultiphasicSolver::Quasin(double time)
{
	int i, j;
	double s;

	// convergence norms
	double	normR1;		// residual norm
	double	normE1;		// energy norm
	double	normD;		// displacement norm
	double	normd;		// displacement increment norm
	double	normRi;		// initial residual norm
	double	normEi;		// initial energy norm
	double	normEm;		// max energy norm
	double	normDi;		// initial displacement norm

	// poro convergence norms data
	double	normPi;		// initial pressure norm
	double	normP;		// current pressure norm
	double	normp;		// incremement pressure norm

	// solute convergence data
	double	normCi[MAX_CDOFS];	// initial concentration norm
	double	normC[MAX_CDOFS];	// current concentration norm
	double	normc[MAX_CDOFS];	// incremement concentration norm

	// initialize flags
	bool bconv = false;		// convergence flag
	bool breform = false;	// reformation flag

	// get the current step
	FEAnalysis* pstep = m_fem.GetCurrentStep();

	// make sure this is poro-solute problem
	assert(pstep->GetType() == FE_MULTIPHASIC);

	// prepare for the first iteration
	PrepStep(time);

	// do minor iterations callbacks
	m_fem.DoCallback(CB_MINOR_ITERS);

	// calculate initial stiffness matrix
	if (ReformStiffness() == false) return false;

	// calculate initial residual
	if (Residual(m_bfgs.m_R0) == false) return false;

	m_bfgs.m_R0 += m_Fd;

	// TODO: I can check here if the residual is zero.
	// If it is than there is probably no force acting on the system
	// if (m_R0*m_R0 < eps) bconv = true;

//	double r0 = m_R0*m_R0;

	Logfile::MODE oldmode;

	clog.printf("\n===== beginning time step %d : %lg =====\n", pstep->m_ntimesteps+1, m_fem.m_ftime);

	// loop until converged or when max nr of reformations reached
	do
	{
		oldmode = clog.GetMode();
		if ((m_fem.GetCurrentStep()->GetPrintLevel() <= FE_PRINT_MAJOR_ITRS) &&
			(m_fem.GetCurrentStep()->GetPrintLevel() != FE_PRINT_NEVER)) clog.SetMode(Logfile::FILE_ONLY);

		clog.printf(" %d\n", m_niter+1);
		clog.SetMode(oldmode);

		// assume we'll converge. 
		bconv = true;

		// solve the equations
		m_SolverTime.start();
		{
			m_bfgs.SolveEquations(m_bfgs.m_ui, m_bfgs.m_R0);
		}
		m_SolverTime.stop();

		// check for nans
		if (m_fem.GetDebugFlag())
		{
			double du = m_bfgs.m_ui*m_bfgs.m_ui;
			if (ISNAN(du)) throw NANDetected();
		}

		// extract the pressure increments
		GetDisplacementData(m_di, m_bfgs.m_ui);

		// set initial convergence norms
		if (m_niter == 0)
		{
			normRi = fabs(m_bfgs.m_R0*m_bfgs.m_R0);
			normEi = fabs(m_bfgs.m_ui*m_bfgs.m_R0);
			normDi = fabs(m_di*m_di);
			normEm = normEi;
		}

		// perform a linesearch
		// the geometry is also updated in the line search
		if (m_bfgs.m_LStol > 0) s = m_bfgs.LineSearch(1.0);
		else
		{
			s = 1;

			// Update geometry
			Update(m_bfgs.m_ui);

			// calculate residual at this point
			Residual(m_bfgs.m_R1);
		}

		// update all degrees of freedom
		for (i=0; i<m_neq; ++i) m_Ui[i] += s*m_bfgs.m_ui[i];

		// update displacements
		for (i=0; i<m_ndeq; ++i) m_Di[i] += s*m_di[i];

		// calculate norms
		normR1 = m_bfgs.m_R1*m_bfgs.m_R1;
		normd  = (m_di*m_di)*(s*s);
		normD  = m_Di*m_Di;
		normE1 = s*fabs(m_bfgs.m_ui*m_bfgs.m_R1);

		// check residual norm
		if ((m_Rtol > 0) && (normR1 > m_Rtol*normRi)) bconv = false;	

		// check displacement norm
		if ((m_Dtol > 0) && (normd  > (m_Dtol*m_Dtol)*normD )) bconv = false;

		// check energy norm
		if ((m_Etol > 0) && (normE1 > m_Etol*normEi)) bconv = false;

		// check linestep size
		if ((m_bfgs.m_LStol > 0) && (s < m_bfgs.m_LSmin)) bconv = false;

		// check energy divergence
		if (normE1 > normEm) bconv = false;

		// check poroelastic convergence
		{
			// extract the pressure increments
			GetPressureData(m_pi, m_bfgs.m_ui);

			// set initial norm
			if (m_niter == 0) normPi = fabs(m_pi*m_pi);

			// update total pressure
			for (i=0; i<m_npeq; ++i) m_Pi[i] += s*m_pi[i];

			// calculate norms
			normP = m_Pi*m_Pi;
			normp = (m_pi*m_pi)*(s*s);

			// check convergence
			if ((m_Ptol > 0) && (normp > (m_Ptol*m_Ptol)*normP)) bconv = false;
		}

		// check solute convergence
		{
			// extract the concentration increments
			for (j=0; j<MAX_CDOFS; ++j) {
				if (m_nceq[j]) {
					GetConcentrationData(m_ci[j], m_bfgs.m_ui,j);
					
					// set initial norm
					if (m_niter == 0)
						normCi[j] = fabs(m_ci[j]*m_ci[j]);
					
					// update total concentration
					for (i=0; i<m_nceq[j]; ++i) m_Ci[j][i] += s*m_ci[j][i];
					
					// calculate norms
					normC[j] = m_Ci[j]*m_Ci[j];
					normc[j] = (m_ci[j]*m_ci[j])*(s*s);
					
				}
			}
			
			// check convergence
			if (m_Ctol > 0) {
				for (j=0; j<MAX_CDOFS; ++j)
					if (m_nceq[j]) bconv = bconv && (normc[j] <= (m_Ctol*m_Ctol)*normC[j]);
			}
		}

		// print convergence summary
		oldmode = clog.GetMode();
		if ((m_fem.GetCurrentStep()->GetPrintLevel() <= FE_PRINT_MAJOR_ITRS) &&
			(m_fem.GetCurrentStep()->GetPrintLevel() != FE_PRINT_NEVER)) clog.SetMode(Logfile::FILE_ONLY);

		clog.printf(" Nonlinear solution status: time= %lg\n", time); 
		clog.printf("\tstiffness updates             = %d\n", m_bfgs.m_nups);
		clog.printf("\tright hand side evaluations   = %d\n", m_nrhs);
		clog.printf("\tstiffness matrix reformations = %d\n", m_nref);
		if (m_bfgs.m_LStol > 0) clog.printf("\tstep from line search         = %lf\n", s);
		clog.printf("\tconvergence norms :        INITIAL         CURRENT         REQUIRED\n");
		clog.printf("\t residual               %15le %15le %15le\n", normRi, normR1, m_Rtol*normRi);
		clog.printf("\t energy                 %15le %15le %15le\n", normEi, normE1, m_Etol*normEi);
		clog.printf("\t displacement           %15le %15le %15le\n", normDi, normd ,(m_Dtol*m_Dtol)*normD );
		clog.printf("\t fluid pressure         %15le %15le %15le\n", normPi, normp ,(m_Ptol*m_Ptol)*normP );
		for (j=0; j<MAX_CDOFS; ++j) {
			if (m_nceq[j])
				clog.printf("\t solute %d concentration %15le %15le %15le\n", j+1, normCi[j], normc[j] ,(m_Ctol*m_Ctol)*normC[j] );
		}

		clog.SetMode(oldmode);

		// check if we have converged. 
		// If not, calculate the BFGS update vectors
		if (bconv == false)
		{
			if ((normR1 < m_Rmin))
			{
				// check for almost zero-residual on the first iteration
				// this might be an indication that there is no force on the system
				clog.printbox("WARNING", "No force acting on the system.");
				bconv = true;
			}
			else if (s < m_bfgs.m_LSmin)
			{
				// check for zero linestep size
				clog.printbox("WARNING", "Zero linestep size. Stiffness matrix will now be reformed");
				breform = true;
			}
			else if (normE1 > normEm)
			{
				// check for diverging
				clog.printbox("WARNING", "Problem is diverging. Stiffness matrix will now be reformed");
				normEm = normE1;
				normEi = normE1;
				normRi = normR1;
				normDi = normd;
				normPi = normp;
				for (j=0; j<MAX_CDOFS; ++j)
					if (m_nceq[j]) normCi[j] = normc[j];
				breform = true;
			}
			else
			{
				// If we havn't reached max nr of BFGS updates
				// do an update
				if (!breform)
				{
					if (m_bfgs.m_nups < m_bfgs.m_maxups-1)
					{
						if (m_bfgs.Update(s, m_bfgs.m_ui, m_bfgs.m_R0, m_bfgs.m_R1) == false)
						{
							// Stiffness update has failed.
							// this might be due a too large condition number
							// or the update was no longer positive definite.
							clog.printbox("WARNING", "The BFGS update has failed.\nStiffness matrix will now be reformed.");
							breform = true;
						}
					}
					else
					{
						// we've reached the max nr of BFGS updates, so
						// we need to do a stiffness reformation
						breform = true;

						// print a warning only if the user did not intent full-Newton
						if (m_bfgs.m_maxups > 0)
							clog.printbox("WARNING", "Max nr of iterations reached.\nStiffness matrix will now be reformed.");

					}
				}
			}	

			// zero displacement increments
			// we must set this to zero before the reformation
			// because we assume that the prescribed displacements are stored 
			// in the m_ui vector.
			zero(m_bfgs.m_ui);

			// reform stiffness matrices if necessary
			if (breform)
			{
				clog.printf("Reforming stiffness matrix: reformation #%d\n\n", m_nref);

				// reform the matrix
				if (ReformStiffness() == false) break;
	
				// reset reformation flag
				breform = false;
			}

			// copy last calculated residual
			m_bfgs.m_R0 = m_bfgs.m_R1;
		}
		else if (pstep->m_baugment)
		{
			// we have converged, so let's see if the augmentations have converged as well

			clog.printf("\n........................ augmentation # %d\n", m_naug+1);

			// do the augmentations
			bconv = Augment();

			// update counter
			++m_naug;

			// we reset the reformations counter
			m_nref = 0;
	
			// If we havn't converged we prepare for the next iteration
			if (!bconv) 
			{
				// Since the Lagrange multipliers have changed, we can't just copy 
				// the last residual but have to recalculate the residual
				// we also recalculate the stresses in case we are doing augmentations
				// for incompressible materials
				UpdateStresses();
				Residual(m_bfgs.m_R0);

				// reform the matrix if we are using full-Newton
				if (m_bfgs.m_maxups == 0)
				{
					clog.printf("Reforming stiffness matrix: reformation #%d\n\n", m_nref);
					if (ReformStiffness() == false) break;
				}
			}
		}
	
		// increase iteration number
		m_niter++;

		// let's flush the logfile to make sure the last output will not get lost
		clog.flush();

		// do minor iterations callbacks
		m_fem.DoCallback(CB_MINOR_ITERS);
	}
	while (bconv == false);

	// when converged, 
	// print a convergence summary to the clog file
	if (bconv)
	{
		Logfile::MODE mode = clog.SetMode(Logfile::FILE_ONLY);
		if (mode != Logfile::NEVER)
		{
			clog.printf("\nconvergence summary\n");
			clog.printf("    number of iterations   : %d\n", m_niter);
			clog.printf("    number of reformations : %d\n", m_nref);
		}
		clog.SetMode(mode);
	}

	// if converged we update the total displacements
	if (bconv)
	{
		m_Ut += m_Ui;
	}

	return bconv;
}

//-----------------------------------------------------------------------------
//! calculates the residual vector
//! Note that the concentrated nodal forces are not calculated here.
//! This is because they do not depend on the geometry 
//! so we only calculate them once (in Quasin) and then add them here.

bool FEMultiphasicSolver::Residual(vector<double>& R)
{
	int i;
	double dt = m_fem.GetCurrentStep()->m_dt;

	// initialize residual with concentrated nodal loads
	R = m_Fn;

	// zero nodal reaction forces
	zero(m_Fr);

	// setup global RHS vector
	FEResidualVector RHS(GetFEModel(), R, m_Fr);

	// zero rigid body reaction forces
	int NRB = m_fem.Objects();
	for (i=0; i<NRB; ++i)
	{
		FERigidBody& RB = dynamic_cast<FERigidBody&>(*m_fem.Object(i));
		RB.m_Fr = RB.m_Mr = vec3d(0,0,0);
	}

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

/*	// loop over all domains
	for (i=0; i<mesh.Domains(); ++i) 
	{
		FEElasticDomain& dom = dynamic_cast<FEElasticDomain&>(mesh.Domain(i));
		dom.Residual(this, R);
	}
*/

	// internal stress work
	for (i=0; i<mesh.Domains(); ++i)
	{
		FEElasticDomain& dom = dynamic_cast<FEElasticDomain&>(mesh.Domain(i));
		dom.InternalForces(RHS);
	}

	if (m_fem.GetCurrentStep()->m_nanalysis == FE_STEADY_STATE)
	{
		for (i=0; i<mesh.Domains(); ++i) 
		{
			FEMultiphasicDomain* pdom = dynamic_cast<FEMultiphasicDomain*>(&mesh.Domain(i));
			if (pdom)
			{
				pdom->InternalFluidWorkSS (this, R, dt);
				pdom->InternalSoluteWorkSS(this, R, dt);
			}
		}
	}
	else
	{
		for (i=0; i<mesh.Domains(); ++i) 
		{
			FEMultiphasicDomain* pdom = dynamic_cast<FEMultiphasicDomain*>(&mesh.Domain(i));
			if (pdom)
			{
 				pdom->InternalFluidWork (this, R, dt);
				pdom->InternalSoluteWork(this, R, dt);
			}
		}
	}

	// calculate forces due to surface loads
	int nsl = m_fem.SurfaceLoads();
	for (i=0; i<nsl; ++i)
	{
		FESurfaceLoad* psl = m_fem.SurfaceLoad(i);
		if (psl->IsActive()) psl->Residual(RHS);
	}

	// calculate contact forces
	if (m_fem.SurfacePairInteractions() > 0)
	{
		ContactForces(RHS);
	}

	// calculate linear constraint forces
	// note that these are the linear constraints
	// enforced using the augmented lagrangian
	NonLinearConstraintForces(RHS);

	// set the nodal reaction forces
	// TODO: Is this a good place to do this?
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		node.m_Fr = vec3d(0,0,0);

		int n;
		if ((n = -node.m_ID[DOF_X]-2) >= 0) node.m_Fr.x = -m_Fr[n];
		if ((n = -node.m_ID[DOF_Y]-2) >= 0) node.m_Fr.y = -m_Fr[n];
		if ((n = -node.m_ID[DOF_Z]-2) >= 0) node.m_Fr.z = -m_Fr[n];
	}

	// increase RHS counter
	m_nrhs++;

	return true;
}

//-----------------------------------------------------------------------------
//! Calculates global stiffness matrix.

bool FEMultiphasicSolver::StiffnessMatrix(const FETimePoint& tp)
{
	// get the stiffness matrix
	SparseMatrix& K = *m_pK;

	// zero stiffness matrix
	K.zero();

	// zero the residual adjustment vector
	zero(m_Fd);

	// element stiffness matrix
	matrix ke;

	// nodal degrees of freedom
	int i, j, I;

	// get the mesh
	FEMesh& mesh = m_fem.GetMesh();

	// calculate the stiffness matrix for each domain
	FEAnalysis* pstep = m_fem.GetCurrentStep();
	bool bsymm = m_bsymm;
	double dt = pstep->m_dt;
	if (pstep->m_nanalysis == FE_STEADY_STATE)
	{
		for (i=0; i<mesh.Domains(); ++i) 
		{
			FEMultiphasicDomain* pdom = dynamic_cast<FEMultiphasicDomain*>(&mesh.Domain(i));
			if (pdom) pdom->StiffnessMatrixSS(this, bsymm, tp);
		}
	}
	else
	{
		for (i=0; i<mesh.Domains(); ++i) 
		{
			FEMultiphasicDomain* pdom = dynamic_cast<FEMultiphasicDomain*>(&mesh.Domain(i));
			if (pdom) pdom->StiffnessMatrix(this, bsymm, tp);
		}
	}

	// calculate contact stiffness
	if (m_fem.SurfacePairInteractions() > 0) 
	{
		ContactStiffness();
	}

	// calculate stiffness matrices for surface loads
	int nsl = m_fem.SurfaceLoads();
	for (i=0; i<nsl; ++i)
	{
		FESurfaceLoad* psl = m_fem.SurfaceLoad(i);

		// respect the pressure stiffness flag
		if ((dynamic_cast<FEPressureLoad*>(psl) == 0) || (m_fem.GetCurrentStep()->m_istiffpr != 0)) psl->StiffnessMatrix(this); 
	}

	// calculate nonlinear constraint stiffness
	// note that this is the contribution of the 
	// constrainst enforced with augmented lagrangian
	NonLinearConstraintStiffness();

	// we still need to set the diagonal elements to 1
	// for the prescribed rigid body dofs.
	int NRB = m_fem.Objects();
	for (i=0; i<NRB; ++i)
	{
		FERigidBody& rb = dynamic_cast<FERigidBody&>(*m_fem.Object(i));
		for (j=0; j<6; ++j)
			if (rb.m_LM[j] < -1)
			{
				I = -rb.m_LM[j]-2;
				K.set(I,I, 1);
			}
	}

	// let's check the stiffness matrix for zero diagonal elements
	if (m_fem.GetDebugFlag())
	{
		vector<int> zd;
		int neq = K.Size();
		for (i=0; i<neq; ++i)
		{
			if (K.diag(i) == 0) zd.push_back(i);
		}

//		if (zd.empty() == false) throw ZeroDiagonal(zd, m_fem);
		if (zd.empty() == false) throw ZeroDiagonal(-1, -1);
	}

	return true;
}

//-----------------------------------------------------------------------------
void FEMultiphasicSolver::GetDisplacementData(vector<double> &di, vector<double> &ui)
{
	int N = m_fem.GetMesh().Nodes(), nid, m = 0;
	zero(di);
	for (int i=0; i<N; ++i)
	{
		FENode& n = m_fem.GetMesh().Node(i);
		nid = n.m_ID[DOF_X];
		if (nid != -1)
		{
			nid = (nid < -1 ? -nid-2 : nid);
			di[m++] = ui[nid];
			assert(m <= (int) di.size());
		}
		nid = n.m_ID[DOF_Y];
		if (nid != -1)
		{
			nid = (nid < -1 ? -nid-2 : nid);
			di[m++] = ui[nid];
			assert(m <= (int) di.size());
		}
		nid = n.m_ID[DOF_Z];
		if (nid != -1)
		{
			nid = (nid < -1 ? -nid-2 : nid);
			di[m++] = ui[nid];
			assert(m <= (int) di.size());
		}
	}
}

//-----------------------------------------------------------------------------
void FEMultiphasicSolver::GetPressureData(vector<double> &pi, vector<double> &ui)
{
	int N = m_fem.GetMesh().Nodes(), nid, m = 0;
	zero(pi);
	for (int i=0; i<N; ++i)
	{
		FENode& n = m_fem.GetMesh().Node(i);
		nid = n.m_ID[DOF_P];
		if (nid != -1)
		{
			nid = (nid < -1 ? -nid-2 : nid);
			pi[m++] = ui[nid];
			assert(m <= (int) pi.size());
		}
	}
}

//-----------------------------------------------------------------------------
void FEMultiphasicSolver::GetConcentrationData(vector<double> &ci, vector<double> &ui, const int sol)
{
	int N = m_fem.GetMesh().Nodes(), nid, m = 0;
	zero(ci);
	for (int i=0; i<N; ++i)
	{
		FENode& n = m_fem.GetMesh().Node(i);
		nid = n.m_ID[DOF_C+sol];
		if (nid != -1)
		{
			nid = (nid < -1 ? -nid-2 : nid);
			ci[m++] = ui[nid];
			assert(m <= (int) ci.size());
		}
	}
}


//-----------------------------------------------------------------------------
//! Update the model's kinematic data. This is overriden from FEBiphasicSolver so
//! that solute data is updated
void FEMultiphasicSolver::UpdateKinematics(vector<double>& ui)
{
	// first update all solid-mechanics kinematics
	FESolidSolver::UpdateKinematics(ui);

	// update poroelastic data
	UpdatePoro(ui);

	// update solute-poroelastic data
	UpdateSolute(ui);
}

//-----------------------------------------------------------------------------
//! Updates the poroelastic data
void FEMultiphasicSolver::UpdatePoro(vector<double>& ui)
{
	int i, n;

	FEMesh& mesh = m_fem.GetMesh();
	FEAnalysis* pstep = m_fem.GetCurrentStep();

	// update poro-elasticity data
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);

		// update nodal pressures
		n = node.m_ID[DOF_P];
		if (n >= 0) node.m_pt = 0 + m_Ut[n] + m_Ui[n] + ui[n];
	}

	// update poro-elasticity data
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);

		// update velocities
		node.m_vt  = (node.m_rt - node.m_rp) / pstep->m_dt;
	}

	// make sure the prescribed pressures are fullfilled
	int ndis = m_fem.PrescribedBCs();
	for (i=0; i<ndis; ++i)
	{
		FEPrescribedBC& dc = *m_fem.PrescribedBC(i);
		if (dc.IsActive())
		{
			int n    = dc.node;
			int lc   = dc.lc;
			int bc   = dc.bc;
			double s = dc.s;
			double r = dc.r;	// GAA

			FENode& node = mesh.Node(n);

			if (bc == DOF_P) node.m_pt = r + s*m_fem.GetLoadCurve(lc)->Value(); // GAA
		}
	}
}

//-----------------------------------------------------------------------------
//! Updates the solute data
void FEMultiphasicSolver::UpdateSolute(vector<double>& ui)
{
	int i, j, n;
	
	FEMesh& mesh = m_fem.GetMesh();
	FEAnalysis* pstep = m_fem.GetCurrentStep();
	
	// update solute data
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		
		// update nodal concentration
		for (j=0; j<MAX_CDOFS; ++j) {
			n = node.m_ID[DOF_C+j];
//			if (n >= 0) node.m_ct[j] = 0 + m_Ut[n] + m_Ui[n] + ui[n];
			// Force the concentrations to remain positive
			if (n >= 0) {
				node.m_ct[j] = 0 + m_Ut[n] + m_Ui[n] + ui[n];
				if (node.m_ct[j] < 0) {
					node.m_ct[j] = 0;
				}
			}
		}
	}
	
	// update solute data
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);
		
		// update velocities
		node.m_vt  = (node.m_rt - node.m_rp) / pstep->m_dt;
	}
	
	// make sure the prescribed concentrations are fullfilled
	int ndis = m_fem.PrescribedBCs();
	for (i=0; i<ndis; ++i)
	{
		FEPrescribedBC& dc = *m_fem.PrescribedBC(i);
		if (dc.IsActive())
		{
			int n    = dc.node;
			int lc   = dc.lc;
			int bc   = dc.bc;
			double s = dc.s;
			double r = dc.r;	// GAA
			
			FENode& node = mesh.Node(n);
			
			for (j=0; j<MAX_CDOFS; ++j) {
				if (bc == DOF_C+j) node.m_ct[j] = r + s*m_fem.GetLoadCurve(lc)->Value();
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FEMultiphasicSolver::UpdateContact()
{
	FEAnalysis* pstep = m_fem.GetCurrentStep();

	// mark all free-draining surfaces
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i) 
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));

		FESlidingInterface2* psi2 = dynamic_cast<FESlidingInterface2*>(pci);
		if (psi2) psi2->MarkFreeDraining();
		FESlidingInterface3* psi3 = dynamic_cast<FESlidingInterface3*>(pci);
		if (psi3) psi3->MarkAmbient();
	}

	// Update all contact interfaces
	FESolidSolver::UpdateContact();

	// set free-draining boundary conditions
	for (int i=0; i<m_fem.SurfacePairInteractions(); ++i) 
	{
		FEContactInterface* pci = dynamic_cast<FEContactInterface*>(m_fem.SurfacePairInteraction(i));

		FESlidingInterface2* psi2 = dynamic_cast<FESlidingInterface2*>(pci);
		if (psi2) psi2->SetFreeDraining();
		FESlidingInterface3* psi3 = dynamic_cast<FESlidingInterface3*>(pci);
		if (psi3) psi3->SetAmbient();
	}
}

//-----------------------------------------------------------------------------
//! Save data to dump file

void FEMultiphasicSolver::Serialize(DumpFile& ar)
{
	FESolidSolver::Serialize(ar);

	if (ar.IsSaving())
	{
		ar << m_Ptol;
		ar << m_ndeq << m_npeq;
	}
	else
	{
		ar >> m_Ptol;
		ar >> m_ndeq >> m_npeq;
		ar >> m_ndeq >> m_npeq;
	}

	if (ar.IsSaving())
	{
		ar << m_Ctol;
		for (int i=0; i<MAX_CDOFS; ++i) ar << m_nceq[i];
	}
	else
	{
		ar >> m_Ctol;
		for (int i=0; i<MAX_CDOFS; ++i) ar >> m_nceq[i];
	}
}