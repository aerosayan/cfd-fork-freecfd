/************************************************************************
	
	Copyright 2007-2008 Emre Sozer & Patrick Clark Trizila

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
#ifndef GRID_H
#define GRID_H

/*
  Classes for reading and storing grid information
*/

#include <cgnslib.h>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <mpi.h>
#include "vec3d.h"
using namespace std;
#include <parmetis.h>

class Node : public Vec3D {
public:
	unsigned int id, globalId;
	std::vector<int> cells;
	std::vector<int> ghosts;
	std::map<int,double> average;
	std::set<int> bcs;
	Node(double x=0., double y=0., double z=0.);
};

class Face {
public:
	int bc;
	unsigned int id;
	int parent,neighbor;
	unsigned int nodeCount;
	Vec3D centroid;
	Vec3D normal;
	std::map<int,double> average;
	double area;
	std::vector<int> nodes;
	Node& node(int n);
};

class Cell {
public:
	unsigned int nodeCount,faceCount,neighborCellCount,ghostCount,globalId,globalCellCount;
	ElementType_t type; // TODO get rid of this one
	double volume,lengthScale;
	Vec3D centroid;
	std::vector<int> nodes;
	std::vector<int> faces;
	std::vector<int> neighborCells;
	std::vector<int> ghosts;
	double rho,p,k,omega,mu;
	Vec3D v,grad[7],limited_grad[7];
	std::map<int,Vec3D> gradMap;
	double flux[7];
	double update[7];
	Cell(void);
	bool HaveNodes(unsigned int &nodelistsize, unsigned int nodelist[]) ;
	Node& node(int n);
	Face& face(int f);
};

class Ghost {
public:
	unsigned int partition;
	unsigned int globalId;
	std::vector<unsigned int> cells;
	double rho;
	Vec3D v,centroid,grad[7],limited_grad[7];
	double p,k,omega,mu;
};

class Grid {
public:
	string fileName;
	unsigned int nodeCount,cellCount,faceCount;
	unsigned int globalNodeCount,globalCellCount,globalFaceCount,ghostCount;
	std::vector<Node> node;
	std::vector<Face> face;
	std::vector<Cell> cell;
	std::vector<Ghost> ghost;
	Grid();
	int read(string);
	int readCGNS();
	int readTEC();
	//int reorderRCM();
	int scale();
	int partition();
	int mesh2dual();
	int create_nodes_cells();
	int create_faces();
	int create_ghosts();
	int areas_volumes();
	void nodeAverages();
	void nodeAverages_new();
	void nodeAverages_idw();
	void faceAverages();
	void gradMaps();
	void gradients();
 	void limit_gradients(void);
	void lengthScales(void);
};


class GridRawData { // The data will be destroyed after processing
public:
	std::vector<double> x,y,z;
	std::vector<int> cellConnIndex,cellConnectivity;
	std::vector< vector<int> > bocoConnIndex,bocoConnectivity;
	std::map<string,int> bocoNameMap;
};

class IndexMaps { // The data will be destroyed after processing
public:
	std::vector<unsigned int> cellOwner; // takes cell global id and returns the owner rank
	std::map<unsigned int,unsigned int> nodeGlobal2Local;
	std::map<unsigned int,unsigned int> cellGlobal2Local;
	std::map<unsigned int,unsigned int> ghostGlobal2Local;
	// This stores the bc region numbers that 'some' nodes touch to
	// Those 'some' nodes happen to be the first nodes in each bc face connectivity list
	std::map<unsigned int, vector<int> > nodeBCregions;
	// For those 'some' nodes, this stores the conn list of the corresponsing bc faces
	std::map<unsigned int, vector<set<unsigned int> > > nodeBCregionFaceConn;

	idxtype* adjIndex;
	idxtype* adjacency;
};


#endif
