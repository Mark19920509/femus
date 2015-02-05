//C++ includes 
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <cstdlib>
#include <sstream>

// FEMuS
#include "FEMTTUConfig.h"
#include "paral.hpp"
#include "FemusDefault.hpp"
#include "FemusInit.hpp"
#include "Files.hpp"
#include "MultiLevelMeshTwo.hpp"
#include "GenCase.hpp"
#include "FETypeEnum.hpp"
#include "ElemType.hpp"
#include "TimeLoop.hpp"
#include "Typedefs.hpp"
#include "CmdLine.hpp"
#include "Quantity.hpp"
#include "QTYnumEnum.hpp"
#include "Box.hpp"  //for the DOMAIN
#include "XDMFWriter.hpp"


// application includes
#include "OptLoop.hpp"
#include "OptQuantities.hpp"
#include "EqnNS.hpp"
#include "EqnNSAD.hpp"
#include "EqnMHD.hpp"
#include "EqnMHDAD.hpp"
#include "EqnMHDCONT.hpp"

#ifdef HAVE_LIBMESH
#include "libmesh/libmesh.h"
#endif

using namespace femus;


// double funzione(double t , const double* xyz) {return 1.;} 

// =======================================
// MHD optimal control problem
// =======================================

int main(int argc, char** argv) {

#ifdef HAVE_LIBMESH
   libMesh::LibMeshInit init(argc,argv);
#else   
   FemusInit init(argc,argv);
#endif 

// ======= Files ========================
  Files files;
        files.ConfigureRestart();
        files.CheckIODirectories();
        files.CopyInputFiles();
        files.RedirectCout();
	
  // ======= Physics ========================
  FemusInputParser<double> physics_map("Physics",files.GetOutputPath());
  
  const double rhof   = physics_map.get("rho0");
  const double Uref   = physics_map.get("Uref");
  const double Lref   = physics_map.get("Lref");
  const double  muf   = physics_map.get("mu0");
  const double MUMHD  = physics_map.get("MUMHD");
  const double SIGMHD = physics_map.get("SIGMHD");
  const double   Bref = physics_map.get("Bref");
  const double sigma  = physics_map.get("sigma");

  const double   _pref = rhof*Uref*Uref;             physics_map.set("pref",_pref);
  const double   _Re  = (rhof*Uref*Lref)/muf;        physics_map.set("Re",_Re);
  const double   _Fr  = (Uref*Uref)/(9.81*Lref);     physics_map.set("Fr",_Fr);
  const double   _Pr=muf/rhof;                       physics_map.set("Pr",_Pr);

  const double   _Rem = MUMHD*SIGMHD*Uref*Lref;      physics_map.set("Rem",_Rem);
  const double   _Hm  = Bref*Lref*sqrt(SIGMHD/muf);  physics_map.set("Hm",_Hm);
  const double   _S   = _Hm*_Hm/(_Re*_Rem);          physics_map.set("S",_S);
  
  const double   _We  = (Uref*Uref*Lref*rhof)/sigma; physics_map.set("We",_We);

  // ======= Mesh =====
  FemusInputParser<double> mesh_map("Mesh",files.GetOutputPath());
    GenCase mesh(mesh_map,"straightQ3D2x2x2ZERO.gam");
          mesh.SetLref(1.);  
	  
  // ======= MyDomainShape  (optional, implemented as child of Domain) ====================
  FemusInputParser<double> box_map("Box",files.GetOutputPath());
  Box mybox(mesh.get_dim(),box_map);
      mybox.InitAndNondimensionalize(mesh.get_Lref());

          mesh.SetDomain(&mybox);    
	  
          mesh.GenerateCase(files.GetOutputPath());

          mesh.SetLref(Lref);
      mybox.InitAndNondimensionalize(mesh.get_Lref());
	  
          XDMFWriter::ReadMeshAndNondimensionalizeBiquadraticHDF5(files.GetOutputPath(),mesh); 
	  XDMFWriter::PrintMeshXDMF(files.GetOutputPath(),mesh,BIQUADR_FE);
          XDMFWriter::PrintMeshLinear(files.GetOutputPath(),mesh);
      
  // ===== QuantityMap =========================================
  QuantityMap  qty_map;
  qty_map.SetMeshTwo(&mesh);
  qty_map.SetInputParser(&physics_map);
//================================
// ======= Add QUANTITIES ========  
//================================
  MagnFieldHom bhom("Qty_MagnFieldHom",qty_map,mesh.get_dim(),QQ);     qty_map.AddQuantity(&bhom);  
  MagnFieldExt Bext("Qty_MagnFieldExt",qty_map,mesh.get_dim(),QQ);     qty_map.AddQuantity(&Bext);  

//consistency check
 if (bhom._dim !=  Bext._dim)     {std::cout << "main: inconsistency" << std::endl;abort();}
 if (bhom._FEord !=  Bext._FEord) {std::cout << "main: inconsistency" << std::endl;abort();}

 MagnFieldHomLagMult         bhom_lag_mult("Qty_MagnFieldHomLagMult",qty_map,1,LL);     qty_map.AddQuantity(&bhom_lag_mult);
 MagnFieldExtLagMult         Bext_lag_mult("Qty_MagnFieldExtLagMult",qty_map,1,LL);     qty_map.AddQuantity(&Bext_lag_mult);
 MagnFieldHomAdj                  bhom_adj("Qty_MagnFieldHomAdj",qty_map,mesh.get_dim(),QQ);        qty_map.AddQuantity(&bhom_adj);
 MagnFieldHomLagMultAdj  bhom_lag_mult_adj("Qty_MagnFieldHomLagMultAdj",qty_map,1,LL);  qty_map.AddQuantity(&bhom_lag_mult_adj);

  Pressure  pressure("Qty_Pressure",qty_map,1,LL);            qty_map.AddQuantity(&pressure);
  Velocity  velocity("Qty_Velocity",qty_map,mesh.get_dim(),QQ);   qty_map.AddQuantity(&velocity);  

  VelocityAdj  velocity_adj("Qty_VelocityAdj",qty_map,mesh.get_dim(),QQ);         qty_map.AddQuantity(&velocity_adj);  
  PressureAdj pressure_adj("Qty_PressureAdj",qty_map,1,LL);                  qty_map.AddQuantity(&pressure_adj);
  DesVelocity des_velocity("Qty_DesVelocity",qty_map,mesh.get_dim(),QQ);       qty_map.AddQuantity(&des_velocity);
 
//consistency check
 if (velocity._dim !=  des_velocity._dim) {std::cout << "main: inconsistency" << std::endl; abort();}
 if (velocity._FEord !=  des_velocity._FEord) {std::cout << "main: inconsistency" << std::endl; abort();}

// #if TEMP_DEPS==1
  Temperature       temperature("Qty_Temperature",qty_map,1,QQ);      qty_map.AddQuantity(&temperature);  
  Density               density("Qty_Density",qty_map,1,QQ);                       qty_map.AddQuantity(&density);   
  Viscosity           viscosity("Qty_Viscosity",qty_map,1,QQ);                     qty_map.AddQuantity(&viscosity);
  HeatConductivity    heat_cond("Qty_HeatConductivity",qty_map,1,QQ);              qty_map.AddQuantity(&heat_cond);
  SpecificHeatP      spec_heatP("Qty_SpecificHeatP",qty_map,1,QQ);                 qty_map.AddQuantity(&spec_heatP);
// #endif  

  
//================================
//==== END Add QUANTITIES ========
//================================

  // ====== MultiLevelProblem =================================
  MultiLevelProblem ml_prob;
  ml_prob.SetMeshTwo(&mesh);
  ml_prob.SetQruleAndElemType("fifth");
  ml_prob.SetInputParser(&physics_map);
  ml_prob.SetQtyMap(&qty_map); 
  
  
//===============================================
//================== Add EQUATIONS  AND ======================
//========= associate an EQUATION to QUANTITIES ========
//========================================================

#if NS_EQUATIONS==1
  EqnNS & eqnNS = ml_prob.add_system<EqnNS>("Eqn_NS",NO_SMOOTHER);
          eqnNS.AddUnknownToSystemPDE(&velocity); 
          eqnNS.AddUnknownToSystemPDE(&pressure); 
#endif
  
#if NSAD_EQUATIONS==1
  EqnNSAD & eqnNSAD = ml_prob.add_system<EqnNSAD>("Eqn_NSAD",NO_SMOOTHER); 
            eqnNSAD.AddUnknownToSystemPDE(&velocity_adj); 
            eqnNSAD.AddUnknownToSystemPDE(&pressure_adj); 
#endif
  
#if MHD_EQUATIONS==1
  EqnMHD & eqnMHD = ml_prob.add_system<EqnMHD>("Eqn_MHD",NO_SMOOTHER);
           eqnMHD.AddUnknownToSystemPDE(&bhom); 
           eqnMHD.AddUnknownToSystemPDE(&bhom_lag_mult); 
#endif

#if MHDAD_EQUATIONS==1
  EqnMHDAD & eqnMHDAD = ml_prob.add_system<EqnMHDAD>("Eqn_MHDAD",NO_SMOOTHER);
             eqnMHDAD.AddUnknownToSystemPDE(&bhom_adj); 
             eqnMHDAD.AddUnknownToSystemPDE(&bhom_lag_mult_adj); 
#endif

#if MHDCONT_EQUATIONS==1
  EqnMHDCONT & eqnMHDCONT = ml_prob.add_system<EqnMHDCONT>("Eqn_MHDCONT",NO_SMOOTHER);
               eqnMHDCONT.AddUnknownToSystemPDE(&Bext); 
               eqnMHDCONT.AddUnknownToSystemPDE(&Bext_lag_mult); 
#endif  
   
//================================
//========= End add EQUATIONS  and ========
//========= associate an EQUATION to QUANTITIES ========
//================================

   for (MultiLevelProblem::const_system_iterator eqn = ml_prob.begin(); eqn != ml_prob.end(); eqn++) {
        SystemTwo* sys = static_cast<SystemTwo*>(eqn->second);
//=====================
    sys -> init_sys();
//=====================
    sys -> _dofmap.ComputeMeshToDof();
//=====================
    sys -> initVectors();
//=====================
    sys -> Initialize();
//=====================
    sys -> _bcond.GenerateBdc();
    sys -> _bcond.GenerateBdcElem();
//=====================
    sys -> ReadMGOps(files.GetOutputPath());
    
    }

  // ======== OptLoop ===================================
  FemusInputParser<double> loop_map("TimeLoop",files.GetOutputPath());
  OptLoop opt_loop(files,loop_map); 

  opt_loop.TransientSetup(ml_prob);  // reset the initial state (if restart) and print the Case   /*TODO fileIO */ 

  opt_loop.optimization_loop(ml_prob);
    
//============= prepare default for next restart ==========  
// at this point, the run has been completed 
// well, we do not know whether for the whole time range time.N-M.xmf
// or the optimization loop stopped before
// we could also print the last step number
  files.PrintRunForRestart(DEFAULT_LAST_RUN); /*(iproc==0)*/ 
  files.log_petsc();

// ============  clean ================================
  ml_prob.clear();
  mesh.clear();
  
  return 0;
  
}
