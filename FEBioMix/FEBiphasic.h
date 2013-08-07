#pragma once
#include "FEBioMech/FEElasticMaterial.h"

//-----------------------------------------------------------------------------
//! Biphasic material point class.
//
class FEBiphasicMaterialPoint : public FEMaterialPoint
{
public:
	//! constructor
	FEBiphasicMaterialPoint(FEMaterialPoint* ppt);

	//! create a shallow copy
	FEMaterialPoint* Copy();

	//! data serialization
	void Serialize(DumpFile& ar);

	//! Data initialization
	void Init(bool bflag);

public:
	// poro-elastic material data
	// The actual fluid pressure is the same as the effective fluid pressure
	// in a poroelastic material without solute(s).  The actual fluid pressure
	// is included here so that models that include both poroelastic and
	// solute-poroelastic domains produce plotfiles with consistent fluid
	// pressure fields.
	double		m_p;		//!< fluid pressure
	vec3d		m_gradp;	//!< spatial gradient of p
	vec3d		m_w;		//!< fluid flux
	double		m_pa;		//!< actual fluid pressure
	double		m_phi0;		//!< referential solid volume fraction at current time
	double		m_phi0p;	//!< referential solid volume fraction at previous time
	double		m_phi0hat;	//!< referential solid volume fraction supply at current time
	double		m_phi0hatp;	//!< m_phi0hat at previous time
};

//-----------------------------------------------------------------------------
//! Base class for hydraulic permeability of porous materials.
//! These materials need to define the permeability and tangent permeability functions.
//!
class FEHydraulicPermeability : public FEMaterial
{
public:
	FEHydraulicPermeability() {}
	virtual ~FEHydraulicPermeability(){}
		
	//! hydraulic permeability
	virtual mat3ds Permeability(FEMaterialPoint& pt) = 0;
		
	//! tangent of hydraulic permeability with respect to strain
	virtual tens4ds Tangent_Permeability_Strain(FEMaterialPoint& mp) = 0;
		
	//! tangent of hydraulic permeability with respect to concentration
	mat3ds Tangent_Permeability_Concentration(FEMaterialPoint& mp, const int isol);
		
	void Init();
};

//-----------------------------------------------------------------------------
//! Base class for solvent supply.
//! These materials need to define the supply and tangent supply functions.
//!
class FESolventSupply : public FEMaterial
{
public:
	FESolventSupply() {}
	virtual ~FESolventSupply(){}
	
	//! solvent supply
	virtual double Supply(FEMaterialPoint& pt) = 0;
	
	//! tangent of solvent supply with respect to strain
	virtual mat3ds Tangent_Supply_Strain(FEMaterialPoint& mp) = 0;
	
	//! tangent of solvent supply with respect to pressure
	virtual double Tangent_Supply_Pressure(FEMaterialPoint& mp) = 0;
	
	//! tangent of solvent supply with respect to concentration
	double Tangent_Supply_Concentration(FEMaterialPoint& mp, const int isol);
	
	void Init();
};

//-----------------------------------------------------------------------------
//! Base class for biphasic materials.

class FEBiphasic : public FEMultiMaterial
{
public:
	FEBiphasic();
	
	// returns a pointer to a new material point object
	FEMaterialPoint* CreateMaterialPointData() 
	{ 
		return new FEBiphasicMaterialPoint(m_pSolid->CreateMaterialPointData());
	}

	// Get the elastic component (overridden from FEMaterial)
	FEElasticMaterial* GetElasticMaterial() { return m_pSolid->GetElasticMaterial(); }

	// find a material parameter
	FEParam* GetParameter(const ParamString& s);
	
public:
	void Init();
	
	//! calculate stress at material point
	mat3ds Stress(FEMaterialPoint& pt);
	
	//! calculate tangent stiffness at material point
	tens4ds Tangent(FEMaterialPoint& pt);

	//! return the permeability tensor as a matrix
	void Permeability(double k[3][3], FEMaterialPoint& pt);
	
	//! calculate fluid flux
	vec3d Flux(FEMaterialPoint& pt);
	
	//! calculate actual fluid pressure
	double Pressure(FEMaterialPoint& pt);

	//! porosity
	double Porosity(FEMaterialPoint& pt);
	
	//! fluid density
	double FluidDensity() { return m_rhoTw; } 

	//! Serialization
	void Serialize(DumpFile& ar);
	
public: // material parameters
	double						m_rhoTw;	//!< true fluid density
	double						m_phi0;		//!< solid volume fraction in reference configuration

public: // material properties
	FEElasticMaterial*			m_pSolid;	//!< pointer to elastic solid material
	FEHydraulicPermeability*	m_pPerm;	//!< pointer to permeability material
	FESolventSupply*			m_pSupp;	//!< pointer to solvent supply
	
	DECLARE_PARAMETER_LIST();
};