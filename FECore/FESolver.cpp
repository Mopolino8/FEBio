#include "stdafx.h"
#include "FESolver.h"

//-----------------------------------------------------------------------------
FESolver::FESolver(FEModel& fem) : m_fem(fem)
{ 
	m_bsymm = true; // assume symmetric stiffness matrix
	m_solvertype = 0;
	m_niter = 0;
}

//-----------------------------------------------------------------------------
FESolver::~FESolver()
{
}

//-----------------------------------------------------------------------------
//! Get the FE model
FEModel& FESolver::GetFEModel()
{ 
	return m_fem; 
}