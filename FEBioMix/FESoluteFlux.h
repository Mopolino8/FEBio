#pragma once
#include "FECore/FESurfaceLoad.h"

//-----------------------------------------------------------------------------
//! The flux surface is a surface domain that sustains a solute flux boundary
//! condition
//!
class FESoluteFlux : public FESurfaceLoad
{
public:
	struct LOAD
	{
		LOAD() { s[0] = s[1] = s[2] = s[3] = s[4] = s[5] = s[6] = s[7] = 1.0; bc = 0; }
		
		double	s[8];		// nodal scale factors
		int		lc;			// load curve
		int		bc;			// degree of freedom
	};

public:
	//! constructor
	FESoluteFlux(FESurface* ps, bool blinear = false, int isol = 0) : FESurfaceLoad(ps) { m_blinear = blinear; m_isol = isol; }
	
	//! allocate storage
	void create(int n) { m_PC.resize(n); }
	
/*	//! clone
	FEDomain* Clone()
	{
		FESoluteFlux* ps = new FESoluteFlux(m_pMesh);
		ps->m_PC = m_PC;
		return ps;
	}
*/

	//! get a flux BC
	LOAD& SoluteFlux(int n) { return m_PC[n]; }
	
	//! calculate flux stiffness
	void StiffnessMatrix(FESolver* psolver);
	
	//! calculate residual
	void Residual(FEGlobalVector& R);
	
	//! serialize data
	void Serialize(DumpFile& ar);

protected:
	//! calculate stiffness for an element
	void FluxStiffness(FESurfaceElement& el, matrix& ke, vector<double>& vn, double dt);
	
	//! Calculates volumetric flow rate due to flux
	bool FlowRate(FESurfaceElement& el, vector<double>& fe, vector<double>& vn, double dt);
	
	//! Calculates the linear volumetric flow rate due to flux (ie. non-follower)
	bool LinearFlowRate(FESurfaceElement& el, vector<double>& fe, vector<double>& vn, double dt);
	
protected:
	bool	m_blinear;	//!< linear or not (true is non-follower, false is follower)
	int		m_isol;		//!< solute index

	// solute flux boundary data
	vector<LOAD>	m_PC;		//!< solute flux boundary cards
};