/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2020 University of Utah, The Trustees of Columbia University in 
the City of New York, and others.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/



#include "stdafx.h"
#include "FEFiberExpPow.h"
#include <FECore/log.h>

// define the material parameters
BEGIN_FECORE_CLASS(FEFiberExpPow, FEElasticFiberMaterial)
	ADD_PARAMETER(m_alpha, FE_RANGE_GREATER_OR_EQUAL(0.0), "alpha");
	ADD_PARAMETER(m_beta , FE_RANGE_GREATER_OR_EQUAL(2.0), "beta");
	ADD_PARAMETER(m_ksi  , FE_RANGE_GREATER_OR_EQUAL(0.0), "ksi" );
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
// FEFiberExpPow
//-----------------------------------------------------------------------------

FEFiberExpPow::FEFiberExpPow(FEModel* pfem) : FEElasticFiberMaterial(pfem)
{ 
}

//-----------------------------------------------------------------------------
mat3ds FEFiberExpPow::FiberStress(FEMaterialPoint& mp, const vec3d& n0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	
	// deformation gradient
	mat3d &F = pt.m_F;
	double J = pt.m_J;
	
	// loop over all integration points
	mat3ds C = pt.RightCauchyGreen();
	mat3ds s;
	
	// Calculate In = n0*C*n0
	double In_1 = n0*(C*n0) - 1.0;
	
	// only take fibers in tension into consideration
	const double eps = 0;
	if (In_1 >= eps)
	{
		// get the global spatial fiber direction in current configuration
		vec3d nt = F*n0;
		
		// calculate the outer product of nt
		mat3ds N = dyad(nt);
		
		// calculate strain energy derivative
		double Wl = m_ksi*pow(In_1, m_beta-1.0)*exp(m_alpha*pow(In_1, m_beta));
		
		// calculate the fiber stress
		s = N*(2.0*Wl/J);
	}
	else
	{
		s.zero();
	}
	
	return s;
}

//-----------------------------------------------------------------------------
tens4ds FEFiberExpPow::FiberTangent(FEMaterialPoint& mp, const vec3d& n0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	
	// deformation gradient
	mat3d &F = pt.m_F;
	double J = pt.m_J;
	
	// loop over all integration points
	mat3ds C = pt.RightCauchyGreen();
	tens4ds c;
	
	// Calculate In = n0*C*n0
	double In_1 = n0*(C*n0) - 1.0;
	
	// only take fibers in tension into consideration
	const double eps = 0;
	if (In_1 >= eps)
	{
		// get the global spatial fiber direction in current configuration
		vec3d nt = F*n0;
		
		// calculate the outer product of nt
		mat3ds N = dyad(nt);
		tens4ds NxN = dyad1s(N);
		
		// calculate strain energy 2nd derivative
		double tmp = m_alpha*pow(In_1, m_beta);
		double Wll = m_ksi*pow(In_1, m_beta-2.0)*((tmp+1)*m_beta-1.0)*exp(tmp);
		
		// calculate the fiber tangent
		c = NxN*(4.0*Wll/J);
	}
	else
	{
		c.zero();
	}
	
	return c;
}

//-----------------------------------------------------------------------------
double FEFiberExpPow::FiberStrainEnergyDensity(FEMaterialPoint& mp, const vec3d& n0)
{
    double sed = 0.0;
    
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	
	// loop over all integration points
	const double eps = 0;
	mat3ds C = pt.RightCauchyGreen();
	
	// Calculate In = n0*C*n0
	double In_1 = n0*(C*n0) - 1.0;
	
	// only take fibers in tension into consideration
	if (In_1 >= eps)
	{
		// calculate strain energy derivative
        if (m_alpha > 0) {
            sed = m_ksi/(m_alpha*m_beta)*(exp(m_alpha*pow(In_1, m_beta))-1);
        }
        else
            sed = m_ksi/m_beta*pow(In_1, m_beta);
	}

    return sed;
}


//-----------------------------------------------------------------------------
// FEFiberExponentialPower
//-----------------------------------------------------------------------------

// define the material parameters
BEGIN_FECORE_CLASS(FEFiberExponentialPower, FEElasticFiberMaterial)
	ADD_PARAMETER(m_alpha, FE_RANGE_GREATER_OR_EQUAL(0.0), "alpha");
	ADD_PARAMETER(m_beta , FE_RANGE_GREATER_OR_EQUAL(2.0), "beta" );
	ADD_PARAMETER(m_ksi  , "ksi"  );
    ADD_PARAMETER(m_mu   , FE_RANGE_GREATER_OR_EQUAL(0.0), "mu"   );
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
FEFiberExponentialPower::FEFiberExponentialPower(FEModel* pfem) : FEElasticFiberMaterial(pfem) 
{
	m_alpha = 0; 
	m_beta = 2; 
	m_ksi = 0; 
	m_mu = 0;
}

//-----------------------------------------------------------------------------
bool FEFiberExponentialPower::Validate()
{
	// TODO: how validate model parameters?
//	if ((4 * m_ksi + 2 * m_mu) < 0) {
//		feLogError("4*ksi+2*mu must be positive."); return false;
//	}
    return FEElasticFiberMaterial::Validate();
}

//-----------------------------------------------------------------------------
mat3ds FEFiberExponentialPower::FiberStress(FEMaterialPoint& mp, const vec3d& n0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
    	
	// deformation gradient
	mat3d &F = pt.m_F;
	double J = pt.m_J;
	
	// loop over all integration points
	mat3ds C = pt.RightCauchyGreen();
	mat3ds s;
	
	// Calculate In = n0*C*n0
	double In_1 = n0*(C*n0) - 1.0;
	
	// only take fibers in tension into consideration
	const double eps = 0;
	if (In_1 >= eps)
	{
		// get the global spatial fiber direction in current configuration
		vec3d nt = F*n0;
		
		// calculate the outer product of nt
		mat3ds N = dyad(nt);
		
		// calculate strain energy derivative
		double Ib = pow(In_1, m_beta - 1.0);
		double Wl = m_ksi(mp)*Ib*exp(m_alpha*(Ib*In_1));
//		Wl = m_ksi*pow(In_1, m_beta-1.0)*exp(m_alpha*pow(In_1, m_beta));
		
		// calculate the fiber stress
		s = N*(2.0*Wl/J);
        
        // add the contribution from shear
        mat3ds BmI = pt.LeftCauchyGreen() - mat3dd(1);
        s += (N*BmI).sym()*(m_mu/J);
	}
	else
	{
		s.zero();
	}
	
	return s;
}

//-----------------------------------------------------------------------------
tens4ds FEFiberExponentialPower::FiberTangent(FEMaterialPoint& mp, const vec3d& n0)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	
	// deformation gradient
	mat3d &F = pt.m_F;
	double J = pt.m_J;
	
	mat3ds C = pt.RightCauchyGreen();
	tens4ds c;
	
	// Calculate In = n0*C*n0
	double In_1 = n0*(C*n0) - 1.0;
	
	// only take fibers in tension into consideration
	const double eps = 0;
	if (In_1 >= eps)
	{
		// get the global spatial fiber direction in current configuration
		vec3d nt = F*n0;
		
		// calculate the outer product of nt
		mat3ds N = dyad(nt);
		tens4ds NxN = dyad1s(N);
		
		// calculate strain energy 2nd derivative
		double tmp = m_alpha*pow(In_1, m_beta);
		double Wll = m_ksi(mp)*pow(In_1, m_beta-2.0)*((tmp+1)*m_beta-1.0)*exp(tmp);
		
		// calculate the fiber tangent
		c = NxN*(4.0*Wll/J);
        
        // add the contribution from shear
        mat3ds B = pt.LeftCauchyGreen();
        c += dyad4s(N,B)*(m_mu/J);
	}
	else
	{
		c.zero();
	}
	
	return c;
}

//-----------------------------------------------------------------------------
//! Strain energy density
double FEFiberExponentialPower::FiberStrainEnergyDensity(FEMaterialPoint& mp, const vec3d& n0)
{
    double sed = 0.0;
    
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	
	// loop over all integration points
	mat3ds C = pt.RightCauchyGreen();
    mat3ds C2 = C.sqr();
	
	// Calculate In = n0*C*n0
	double In_1 = n0*(C*n0) - 1.0;
	
	// only take fibers in tension into consideration
	const double eps = 0;
	if (In_1 >= eps)
	{
		// calculate strain energy density
        if (m_alpha > 0)
            sed = m_ksi(mp)/(m_alpha*m_beta)*(exp(m_alpha*pow(In_1, m_beta))-1);
        else
            sed = m_ksi(mp)/m_beta*pow(In_1, m_beta);
		
        // add the contribution from shear
        sed += m_mu*(n0*(C2*n0)-2*In_1-1)/4.0;
	}

    return sed;
}
