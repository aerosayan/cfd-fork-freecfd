/************************************************************************
	
	Copyright 2007-2010 Emre Sozer

	Contact: emresozer@freecfd.com

	This file is a part of Free CFD

	Free CFD is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.

    Free CFD is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    For a copy of the GNU General Public License,
    see <http://www.gnu.org/licenses/>.

*************************************************************************/
#include "rans.h"

RANS_Model komega,kepsilon;

RANS::RANS(void) {
	kepsilon.sigma_k=1.;
	kepsilon.sigma_omega=0.856;
	kepsilon.beta=0.0828;
	kepsilon.beta_star=0.09;
	kepsilon.kappa=0.41;
	kepsilon.alpha=0.44;
	//kepsilon.alpha=kepsilon.beta/kepsilon.beta_star
	//		-kepsilon.sigma_omega*kepsilon.kappa*kepsilon.kappa/sqrt(kepsilon.beta_star);
	
	komega.sigma_k=0.85;
	komega.sigma_omega=0.5;
	komega.beta=0.075;
	komega.beta_star=0.09;
	komega.kappa=0.41;
	komega.alpha=5./9.;
	//komega.alpha=komega.beta/komega.beta_star
	//		-komega.sigma_omega*komega.kappa*komega.kappa/sqrt(komega.beta_star);
	return; 
} // end RANS::RANS

void RANS::initialize (int ps_max) {
	
	ps_step_max=ps_max;
	nVars=2;
	rtol=input.section("grid",gid).subsection("turbulence").get_double("relativetolerance");
	abstol=input.section("grid",gid).subsection("turbulence").get_double("absolutetolerance");
	maxits=input.section("grid",gid).subsection("turbulence").get_int("maximumiterations");
	
	if (input.section("grid",gid).subsection("turbulence").get_string("model")=="k-omega") {
		model=KOMEGA;
	} else if (input.section("grid",gid).subsection("turbulence").get_string("model")=="k-epsilon") {
		model=KEPSILON;
	} else if (input.section("grid",gid).subsection("turbulence").get_string("model")=="sst") {
		model=SST;
	}

	kLowLimit=input.section("grid",0).subsection("turbulence").get_double("klowlimit");
	kHighLimit=input.section("grid",0).subsection("turbulence").get_double("khighlimit");
	omegaLowLimit=input.section("grid",0).subsection("turbulence").get_double("omegalowlimit");
	viscosityRatioLimit=input.section("grid",0).subsection("turbulence").get_double("viscosityratiolimit");
	Pr_t=input.section("grid",0).subsection("turbulence").get_double("turbulentPr");

	mpi_init();
	material.set(gid);
	create_vars();
	apply_initial_conditions();
	set_bcs();
	mpi_update_ghost_primitives();
	update_boundaries();
	update_eddy_viscosity();
	calc_cell_grads();
	mpi_update_ghost_gradients();
	petsc_init();
	first_residuals.resize(2);
	return;
}

void RANS::solve  (int ts,int pts) {
	timeStep=ts;
	ps_step=pts;
	terms();
	time_terms();
	petsc_solve();
	update_variables();
	mpi_update_ghost_primitives();
	update_boundaries();
	update_eddy_viscosity();
	calc_cell_grads();
	mpi_update_ghost_gradients();
	return;
}

void RANS::create_vars (void) {
	// Allocate variables
	// Default option is to store on cell centers and ghosts only
	k.allocate(gid);
	omega.allocate(gid);
	mu_t.allocate(gid);
	strainRate.allocate(gid);
	gradk.allocate(gid);
	gradomega.allocate(gid);

	update.resize(2);
	for (int i=0; i<2; ++i) update[i].allocate(gid);
	
	yplus.cellStore=false; yplus.allocate(gid);
	
	return;
}

void RANS::apply_initial_conditions (void) {
	// Loop through each initial condition region and apply sequentially
	int count=input.section("grid",gid).subsection("IC",0).count;
	for (int ic=0;ic<count;++ic) {
		// Store the reference to current IC region
		Subsection &region=input.section("grid",gid).subsection("IC",ic);
		Vec3D regionV=region.get_Vec3D("V");
		double intensity,viscRatio;
		// Assign specified values
		intensity=region.get_double("turbulenceintensity");
		viscRatio=region.get_double("eddyviscosityratio");

		// If region is specified with a box method
		if (region.get_string("region")=="box") {
			// Loop the cells
			for (int c=0;c<grid[gid].cell.size();++c) {
				// Check if the cell centroid is inside the box region
				if (withinBox(grid[gid].cell[c].centroid,region.get_Vec3D("corner_1"),region.get_Vec3D("corner_2"))) {
					k.cell(c)=intensity*(fabs(ns[gid].V.cell(c)));
					k.cell(c)*=1.5*k.cell(c);
					k.cell(c)=max(kLowLimit,k.cell(c));
					omega.cell(c)=ns[gid].rho.cell(c)*k.cell(c)/(viscRatio*material.viscosity(ns[gid].T.cell(c)));
				}
			}
		} else if (region.get_string("region")=="cylinder") {
			// Loop the cells
			for (int c=0;c<grid[gid].cell.size();++c) {
				// Check if the cell centroid is inside the cylinder region
				Vec3D axisDirection=region.get_Vec3D("axisdirection");
				axisDirection=axisDirection.norm();
				if (withinCylinder(grid[gid].cell[c].centroid,region.get_Vec3D("center"),region.get_double("radius"),axisDirection,region.get_double("height"))) {
					k.cell(c)=intensity*(fabs(ns[gid].V.cell(c)));
					k.cell(c)*=1.5*k.cell(c);
					k.cell(c)=max(kLowLimit,k.cell(c));
					omega.cell(c)=ns[gid].rho.cell(c)*k.cell(c)/(viscRatio*material.viscosity(ns[gid].T.cell(c)));
				}
			}
		} else if (region.get_string("region")=="sphere") {
			// Loop the cells
			for (int c=0;c<grid[gid].cell.size();++c) {
				// Check if the cell centroid is inside the sphere region
				if (withinSphere(grid[gid].cell[c].centroid,region.get_Vec3D("center"),region.get_double("radius"))) {
					k.cell(c)=intensity*(fabs(ns[gid].V.cell(c)));
					k.cell(c)*=1.5*k.cell(c);
					k.cell(c)=max(kLowLimit,k.cell(c));
					omega.cell(c)=ns[gid].rho.cell(c)*k.cell(c)/(viscRatio*material.viscosity(ns[gid].T.cell(c)));
				}
			}
		}
	}

	// initialize updates
	for (int c=0;c<grid[gid].cell.size();++c) {
		for (int i=0;i<2;++i) {
			update[i].cell(c)=0.;
		}
	}
	
	// Initialize gradients to zero (for internal+ghost cells)
	for (int c=0;c<grid[gid].cell.size();++c) {
		gradk.cell(c)=0.;
		gradomega.cell(c)=0.;
	}

	return;
}

void RANS::calc_cell_grads (void) {
	for (int c=0;c<grid[gid].cellCount;++c) {
		gradk.cell(c)=k.cell_gradient(c);
		gradomega.cell(c)=omega.cell_gradient(c);
	}
	return;
}

void RANS::update_variables(void) {
	
	double residuals[2],ps_residuals[2],totalResiduals[2],total_ps_residuals[2];
	for (int i=0;i<2;++i) {
		residuals[i]=0.;
		ps_residuals[i]=0.;
	}
	
	PetscInt row;
	
	double dt2,dtau2;
	
	for (int c=0;c<grid[gid].cellCount;++c) {
		for (int i=0;i<2;++i) {
			if (isnan(update[i].cell(c)) || isinf(update[i].cell(c))) {
				cerr << "[E] Divergence detected in RANS!...exiting" << endl;
				cerr << "[E] Cell: " << c << " Variable: " << i << endl;
				MPI_Abort(MPI_COMM_WORLD,1);
			}
		}
		
		// Limit the update so that k and omega doesn't end up out of limits
		update[0].cell(c)=max(-1.*(k.cell(c)-kLowLimit),update[0].cell(c));
		update[0].cell(c)=min((kHighLimit-k.cell(c)),update[0].cell(c));
		update[1].cell(c)=max(-1.*(omega.cell(c)-omegaLowLimit),update[1].cell(c));
		
		k.cell(c) += update[0].cell(c);
		omega.cell(c)+= update[1].cell(c);
		
		if (ps_step_max>1) {
			dtau2=dtau[gid].cell(c)*dtau[gid].cell(c);
			ps_residuals[0]+=update[0].cell(c)*update[0].cell(c)/dtau2;
			ps_residuals[1]+=update[1].cell(c)*update[1].cell(c)/dtau2;
		}
		
		// If the last pseudo time step
		if (ps_step_max>1) { // If pseudo time iterations are active
			for (int i=0;i<2;++i) {
				row=(grid[gid].myOffset+c)*2+i;
				VecGetValues(pseudo_delta,1,&row,&update[i].cell(c));
			}
		}
		dt2=dt[gid].cell(c)*dt[gid].cell(c);
		residuals[0]+=update[0].cell(c)*update[0].cell(c)/dt2;
		residuals[1]+=update[1].cell(c)*update[1].cell(c)/dt2;
		
	} // cell loop

	MPI_Allreduce(&residuals,&totalResiduals,2, MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
	if (ps_step_max>1) MPI_Allreduce(&ps_residuals,&total_ps_residuals,2, MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
	
	if (timeStep==1) for (int i=0;i<2;++i) first_residuals[i]=sqrt(totalResiduals[i]);
	if (ps_step_max>1 && ps_step==1) for (int i=0;i<2;++i) first_ps_residuals[i]=sqrt(total_ps_residuals[i]);
	
	res=0.;
	ps_res=0.;
	for (int i=0;i<2;++i) res+=sqrt(totalResiduals[i])/first_residuals[i]/2.;
	if (ps_step_max>1) for (int i=0;i<2;++i) ps_res+=sqrt(total_ps_residuals[i])/first_ps_residuals[i]/2.;
	
	return;
}

