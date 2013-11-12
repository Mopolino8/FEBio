#include "stdafx.h"
#include "FEPreStrainTransIsoMR.h"

//-----------------------------------------------------------------------------
void FEPreStrainMaterialPoint::Init(bool bflag)
{
	if (bflag) m_lam = m_lamp = 1.0;
	else m_lamp = m_lam;
}

//-----------------------------------------------------------------------------
FEMaterialPoint* FEPreStrainMaterialPoint::Copy()
{
	FEPreStrainMaterialPoint* pt = new FEPreStrainMaterialPoint(*this);
	if (m_pt) pt->m_pt = m_pt->Copy();
	return pt;
}

//-----------------------------------------------------------------------------
//! \todo implement this.
void FEPreStrainMaterialPoint::Serialize(DumpFile& ar)
{
	assert(false);
}

//-----------------------------------------------------------------------------
// define the material parameters
BEGIN_PARAMETER_LIST(FEPreStrainTransIsoMR, FETransverselyIsotropic)
	ADD_PARAMETER(c1, FE_PARAM_DOUBLE, "c1");
	ADD_PARAMETER(c2, FE_PARAM_DOUBLE, "c2");
	ADD_PARAMETER(m_ltrg, FE_PARAM_DOUBLE, "pre_stretch");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
mat3ds FEPreStrainTransIsoMR::DevStress(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	// deformation gradient
	mat3d F = pt.m_F;

	// apply in-situ stretch
	if (m_ltrg != 1.0)
	{
		FEPreStrainMaterialPoint& psp = *mp.ExtractData<FEPreStrainMaterialPoint>();

		// set-up local uni-axial stretch tensor
		mat3d U(psp.m_lam, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);

		m_fib.m_lcur = psp.m_lam;

		// transform to local coordinate system
		F = F*(pt.m_Q*U);
	}

//	double J = pt.m_J;
	double J = F.det();

	// calculate deviatoric left Cauchy-Green tensor
	double Jm23 = pow(J, -2.0/3.0);
	mat3ds B = (F*F.transpose()).sym()*Jm23; // pt.DevLeftCauchyGreen();

	// calculate square of B
	mat3ds B2 = B*B;

	// Invariants of B (= invariants of C)
	// Note that these are the invariants of Btilde, not of B!
	double I1 = B.tr();
	double I2 = 0.5*(I1*I1 - B2.tr());

	// --- TODO: put strain energy derivatives here ---
	// Wi = dW/dIi
	double W1 = c1;
	double W2 = c2;
	// ------------------------------------------------

	// calculate T = F*dW/dC*Ft
	mat3ds T = B*(W1 + W2*I1) - B2*W2;

	// calculate stress s = pI + 2/J * dev(T) 
	mat3ds s = T.dev()*(2.0/J);

	// add the fiber stress
	mat3ds fs = m_fib.Stress(mp);
	return s + fs;
}

//-----------------------------------------------------------------------------
//! Calculate deviatoric tangent
tens4ds FEPreStrainTransIsoMR::DevTangent(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	// deformation gradient
	mat3d F = pt.m_F;

	// apply in-situ stretch
	if (m_ltrg != 1.0)
	{
		FEPreStrainMaterialPoint& psp = *mp.ExtractData<FEPreStrainMaterialPoint>();

		// set-up local uni-axial stretch tensor
		mat3d U(psp.m_lam, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);

		m_fib.m_lcur = psp.m_lam;

		// transform to local coordinate system
		F = F*(pt.m_Q*U);
	}

//	double J = pt.m_J;
	double J = F.det();
	double Ji = 1.0/J;

	// calculate deviatoric left Cauchy-Green tensor: B = F*Ft
	double Jm23 = pow(J, -2.0/3.0);
	mat3ds B = (F*F.transpose()).sym()*Jm23; // pt.DevLeftCauchyGreen();

	// calculate square of B
	mat3ds B2 = B*B;

	// Invariants of B (= invariants of C)
	double I1 = B.tr();
	double I2 = 0.5*(I1*I1 - B2.tr());

	// --- TODO: put strain energy derivatives here ---
	// Wi = dW/dIi
	double W1, W2;
	W1 = c1;
	W2 = c2;
	// ------------------------------------

	// calculate dWdC:C
	double WC = W1*I1 + 2*W2*I2;

	// calculate C:d2WdCdC:C
	double CWWC = 2*I2*W2;

	mat3dd I(1);	// Identity
	tens4ds IxI = dyad1s(I);
	tens4ds I4  = dyad4s(I);
	tens4ds BxB = dyad1s(B);
	tens4ds B4  = dyad4s(B);

	// deviatoric cauchy-stress, trs = trace[s]/3
	mat3ds devs = pt.m_s.dev();

	// d2W/dCdC:C
	mat3ds WCCxC = B*(W2*I1) - B2*W2;

	tens4ds cw = (BxB - B4)*(W2*4.0*Ji) - dyad1s(WCCxC, I)*(4.0/3.0*Ji) + IxI*(4.0/9.0*Ji*CWWC);
	tens4ds c = dyad1s(devs, I)*(-2.0/3.0) + (I4 - IxI/3.0)*(4.0/3.0*Ji*WC) + cw;

	return c + m_fib.Tangent(mp);
}