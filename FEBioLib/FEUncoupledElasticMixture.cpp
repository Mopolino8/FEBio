#include "stdafx.h"
#include "FEUncoupledElasticMixture.h"

// define the material parameters
// BEGIN_PARAMETER_LIST(FEUncoupledElasticMixture, FEUncoupledMaterial)
// END_PARAMETER_LIST();

//////////////////////////////////////////////////////////////////////
// Mixture of uncoupled elastic solids
//////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
void FEUncoupledElasticMixture::Init()
{
	FEUncoupledMaterial::Init();
	for (int i=0; i < (int)m_pMat.size(); ++i) {
		m_pMat[i]->Init();
		m_K += m_pMat[i]->m_K;	// Sum up all the values of the bulk moduli
	}
}

//-----------------------------------------------------------------------------
mat3ds FEUncoupledElasticMixture::DevStress(FEMaterialPoint& mp)
{
	mat3ds s;
	
	// calculate stress
	s.zero();
	for (int i=0; i < (int)m_pMat.size(); ++i)
		s += m_pMat[i]->DevStress(mp);
	
	return s;
}

//-----------------------------------------------------------------------------
tens4ds FEUncoupledElasticMixture::DevTangent(FEMaterialPoint& mp)
{
	tens4ds c(0.);
	
	// calculate elasticity tensor
	for (int i=0; i < (int)m_pMat.size(); ++i)
		c += m_pMat[i]->DevTangent(mp);
	
	return c;
}

//-----------------------------------------------------------------------------
//! For elastic mixtures, the parameter name is defined as follows:
//!		material.param
//! where material refers to the name of one of the mixture components and
//! param is the parameter name.
//!
FEParam* FEUncoupledElasticMixture::GetParameter(const ParamString& s)
{
	if (s.count() == 1) return FEUncoupledMaterial::GetParameter(s);

	int NMAT = Materials();
	for (int i=0; i<NMAT; ++i) 
	{
		FEUncoupledMaterial* pmi = GetMaterial(i);
		if (s == pmi->GetName()) return pmi->GetParameter(s.next());
	}

	// no match found
	return 0;
}
