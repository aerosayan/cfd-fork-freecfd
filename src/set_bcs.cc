/************************************************************************
	
	Copyright 2007-2009 Emre Sozer & Patrick Clark Trizila

	Contact: emresozer@freecfd.com , ptrizila@freecfd.com

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
#include "commons.h"
#include <iostream>
#include "inputs.h"
#include "bc.h"
#include "rans.h"
#include "flamelet.h"

extern RANS rans;
extern BC bc;
extern Flamelet flamelet;
bool withinBox(Vec3D point, Vec3D corner_1, Vec3D corner_2);

void setBCs(InputFile &input, BC &bc) {
	
	// Loop through each boundary condition region and apply sequentially
	int count=input.section("boundaryConditions").subsection("BC",0).count;
	for (int b=0;b<count;++b) {
		// Store the reference to current BC region
		Subsection &region=input.section("boundaryConditions").subsection("BC",b);
	
		BCregion bcRegion;
		
		bcRegion.area=0.;
		bcRegion.areaVec=0.;
		bcRegion.momentum=0.;
		
		// Integrate boundary areas
		for (int f=0;f<grid.faceCount;++f) {
			if (grid.face[f].bc==b) {
				bcRegion.area+=grid.face[f].area;
				bcRegion.areaVec+=grid.face[f].area*grid.face[f].normal;
			}
		}
		
		string type=region.get_string("type");
		string kind=region.get_string("kind");
		if (type=="symmetry") bcRegion.type=SYMMETRY;
		if (type=="slip") bcRegion.type=SLIP;
		if (type=="noslip") bcRegion.type=NOSLIP;
		if (type=="inlet") bcRegion.type=INLET;
		if (type=="outlet") bcRegion.type=OUTLET;

		if (kind=="none") bcRegion.kind=NONE;
		if (kind=="noReverse") bcRegion.kind=NO_REVERSE;
		if (kind=="dampReverse") bcRegion.kind=DAMP_REVERSE;
		
		bcRegion.specified=NONE;
		
		bcRegion.k=region.get_double("k");
		bcRegion.omega=region.get_double("omega");
		
		if (!FLAMELET) {
			if (region.get_double("p").is_found) {
				bcRegion.p=region.get_double("p");
				if (region.get_double("T").is_found) {
					if (region.get_double("rho").is_found) {
						cerr << "[E] Thermodynamic state is overspecified in boundary condition BC_" << b+1 << endl;
						exit(1);
					}
					bcRegion.T=region.get_double("T");
					bcRegion.rho=eos.rho(bcRegion.p,bcRegion.T);
					bcRegion.specified=BC_STATE;
					bcRegion.thermalType=FIXED_T;
				} else if (region.get_double("rho").is_found) {
					bcRegion.rho=region.get_double("rho");
					bcRegion.T=eos.T(bcRegion.p,bcRegion.rho);
					bcRegion.specified=BC_STATE;
					bcRegion.thermalType=FIXED_T;
				} else {
					bcRegion.specified=BC_P;
					if (bcRegion.type==NOSLIP || bcRegion.type==SLIP) bcRegion.thermalType=ADIABATIC;
				}
			} else if (region.get_double("T").is_found) {
				bcRegion.T=region.get_double("T");
				if (region.get_double("rho").is_found) {
					bcRegion.rho=region.get_double("rho");
					bcRegion.p=eos.p(bcRegion.rho,bcRegion.T);
					bcRegion.specified=BC_STATE;
					bcRegion.thermalType=FIXED_T;
				} else {
					bcRegion.specified=BC_T;
					bcRegion.thermalType=FIXED_T;
				}
			} else if (region.get_double("rho").is_found) {
				bcRegion.rho=region.get_double("rho");
				bcRegion.specified=BC_RHO;
			} else if (region.get_double("qdot").is_found) {
				if (bcRegion.type==NOSLIP || bcRegion.type==SLIP) {
					bcRegion.thermalType=FIXED_Q;
					bcRegion.qdot=region.get_double("qdot");
				}
			} else {
				if (bcRegion.type==NOSLIP || bcRegion.type==SLIP) bcRegion.thermalType=ADIABATIC;
			}
			

			if (bcRegion.type==SYMMETRY) bcRegion.thermalType=ADIABATIC;
		} else {
			bcRegion.thermalType=ADIABATIC;
			if (region.get_double("Z").is_found) {
				bcRegion.specified=BC_T;
				bcRegion.Z=region.get_double("Z");
				bcRegion.Zvar=region.get_double("Zvar");
				double Chi=2.*bcRegion.omega*bcRegion.Zvar*rans.kepsilon.beta_star;
				bcRegion.rho=flamelet.table.get_rho(bcRegion.Z,bcRegion.Zvar,Chi);	
				bcRegion.T=flamelet.table.get_T(bcRegion.Z,bcRegion.Zvar,Chi,false);
				if (region.get_double("p").is_found) {
					bcRegion.p=region.get_double("p");
					bcRegion.specified=BC_FLAMELET_INLET_P;
				}
				bcRegion.thermalType=FIXED_T;
			} else if (bcRegion.type==OUTLET && region.get_double("p").is_found) {
				bcRegion.specified=BC_P;
			}
		}
		

		
		if (region.get_Vec3D("v").is_found) {
			bcRegion.v=region.get_Vec3D("v");
			bcRegion.kind=VELOCITY;
		}
		if (region.get_double("mdot").is_found) {
			double mdot=region.get_double("mdot");
			bcRegion.mdot=mdot;
			bcRegion.kind=MDOT;
 			//bcRegion.v=-mdot/(bcRegion.rho*bcRegion.area)*bcRegion.areaVec.norm();
		}
		
		if (kind=="none") {
			cout << "[I Rank=" << Rank << "] BC_" << b+1 << " assigned as " << type << endl;
		} else {
			cout << "[I Rank=" << Rank << "] BC_" << b+1 << " assigned as " << type << " of " << kind << " kind" << endl;
		}	
		
		bc.region.push_back(bcRegion);
		
		// Find out pick method
		string pick=region.get_string("pick");
		int pick_from;
		if (pick.substr(0,2)=="BC") {
			pick_from=atoi((pick.substr(2,pick.length())).c_str());
			pick=pick.substr(0,2);
		}
		
		if (region.get_string("region")=="box") {
			for (int f=0;f<grid.faceCount;++f) {
				// if the face is not already marked as internal or partition boundary
				// And if the face centroid falls within the defined box
				if ((grid.face[f].bc>=0 || grid.face[f].bc==UNASSIGNED ) && withinBox(grid.face[f].centroid,region.get_Vec3D("corner_1"),region.get_Vec3D("corner_2"))) {
					if (pick=="overRide") {
						grid.face[f].bc=b; // real boundary conditions are marked as positive
					} else if (pick=="unassigned" && grid.face[f].bc==UNASSIGNED) {
						grid.face[f].bc=b; // real boundary conditions are marked as positive
					} else if (pick=="BC" && grid.face[f].bc==(pick_from-1) ) {
						grid.face[f].bc=b;
 					}
				}
			}
		} 
	}
		
	vector<int> number_of_nsf (np,0); // number of noslip faces in each partition
	
	// Mark nodes that touch boundaries
	for (int f=0;f<grid.faceCount;++f) {
		if (grid.face[f].bc==UNASSIGNED) {
			cerr << "[E Rank=" << Rank << "] Boundary condition could not be found for face " << f << endl;
			exit(1);
		}
		if (grid.face[f].bc>=0) { // if a boundary face
			for (int fn=0;fn<grid.face[f].nodeCount;++fn) {
				grid.face[f].node(fn).bcs.insert(grid.face[f].bc);
			}
			if (bc.region[grid.face[f].bc].type==NOSLIP) number_of_nsf[Rank]++;
		}
	}
	
	grid.boundaryFaceCount.resize(count);
	grid.globalBoundaryFaceCount.resize(count);
	grid.boundaryNodeCount.resize(count);
	grid.globalBoundaryNodeCount.resize(count);
	
	for (int boco=0;boco<count;++boco) {
		grid.boundaryFaceCount[boco]=0;
		grid.globalBoundaryFaceCount[boco]=0;
		grid.boundaryNodeCount[boco]=0;
		grid.globalBoundaryNodeCount[boco]=0;
	}
	
	// Find out number of faces on each bc, locally and globally
	for (int f=0;f<grid.faceCount; ++f) {
		if (grid.face[f].bc>=0) grid.boundaryFaceCount[grid.face[f].bc]++;
	}

	for (int boco=0;boco<count;++boco) {
		cout << "[I Rank=" << Rank << "] Number of Faces on BC_" << boco+1 << "=" << grid.boundaryFaceCount[boco] << endl;
		MPI_Allreduce (&grid.boundaryFaceCount[boco],&grid.globalBoundaryFaceCount[boco],1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	
	// Find out number of nodes on each bc, locally and globally
	for (int n=0;n<grid.nodeCount;++n) {
		for (int boco=0;boco<count;++boco) {
			if (grid.node[n].bcs.find(boco)!=grid.node[n].bcs.end()) {
				if (maps.nodeGlobal2Output[grid.node[n].globalId]>=grid.nodeCountOffset) grid.boundaryNodeCount[boco]++;
			}
		}
	}

	for (int boco=0;boco<count;++boco) {
		MPI_Allreduce (&grid.boundaryNodeCount[boco],&grid.globalBoundaryNodeCount[boco],1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
	}
	
	if (TURBULENCE_MODEL!=NONE) {
		
		MPI_Allgather(&number_of_nsf[Rank],1,MPI_INT,&number_of_nsf[0],1,MPI_INT,MPI_COMM_WORLD);
		
		int nsf_sum=0;
		int displacements[np];
		for (int p=0;p<np;++p) {
			displacements[p]=nsf_sum;
			nsf_sum+=number_of_nsf[p];
		}
		
		vector<double> noSlipFaces_x(nsf_sum);
		vector<double> noSlipFaces_y(nsf_sum);
		vector<double> noSlipFaces_z(nsf_sum);
	
		count=0;
		for (int f=0;f<grid.faceCount;++f) {
			if (grid.face[f].bc>=0 && bc.region[grid.face[f].bc].type==NOSLIP) { 
				noSlipFaces_x[displacements[Rank]+count]=grid.face[f].centroid[0];
				noSlipFaces_y[displacements[Rank]+count]=grid.face[f].centroid[1];
				noSlipFaces_z[displacements[Rank]+count]=grid.face[f].centroid[2];
				count++;
			}
		}
		
		MPI_Allgatherv(&noSlipFaces_x[displacements[Rank]],number_of_nsf[Rank],MPI_DOUBLE,&noSlipFaces_x[0],&number_of_nsf[0],displacements,MPI_DOUBLE,MPI_COMM_WORLD);
		MPI_Allgatherv(&noSlipFaces_y[displacements[Rank]],number_of_nsf[Rank],MPI_DOUBLE,&noSlipFaces_y[0],&number_of_nsf[0],displacements,MPI_DOUBLE,MPI_COMM_WORLD);
		MPI_Allgatherv(&noSlipFaces_z[displacements[Rank]],number_of_nsf[Rank],MPI_DOUBLE,&noSlipFaces_z[0],&number_of_nsf[0],displacements,MPI_DOUBLE,MPI_COMM_WORLD);
		
		Vec3D thisCentroid;
		double thisDistance;
		for (int c=0;c<grid.cellCount;++c) {
			grid.cell[c].closest_wall_distance=1.e20;
			for (int nsf=0;nsf<nsf_sum;++nsf) {
				thisCentroid[0]=noSlipFaces_x[nsf];
				thisCentroid[1]=noSlipFaces_y[nsf];
				thisCentroid[2]=noSlipFaces_z[nsf];
				thisDistance=fabs(grid.cell[c].centroid-thisCentroid);
				if (grid.cell[c].closest_wall_distance>thisDistance) {
					grid.cell[c].closest_wall_distance=thisDistance;
				}
			}
		}
	
		noSlipFaces_x.clear();
		noSlipFaces_y.clear();
		noSlipFaces_z.clear();
	}
	
	return;
}
