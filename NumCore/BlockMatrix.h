#pragma once
#include "FECore/SparseMatrix.h"
#include "CompactMatrix.h"

//-----------------------------------------------------------------------------
// This class implements a diagonally symmetric block-structured matrix. That is
// A matrix for which the diagonal blocks are symmetric, but the off-diagonal
// matrices can be unsymmetric.
class BlockMatrix : public SparseMatrix
{
public:
	struct BLOCK
	{
		int		nstart_row, nend_row;
		int		nstart_col, nend_col;
		CompactMatrix*	pA;
	};

public:
	BlockMatrix();
	~BlockMatrix();

public:
	//! Partition the matrix into blocks
	void Partition(const vector<int>& part);

public:
	//! Create a sparse matrix from a sparse-matrix profile
	void Create(SparseMatrixProfile& MP);

	//! assemble a matrix into the sparse matrix
	void Assemble(matrix& ke, std::vector<int>& lm);

	//! assemble a matrix into the sparse matrix
	void Assemble(matrix& ke, std::vector<int>& lmi, std::vector<int>& lmj);

	//! set entry to value
	void set(int i, int j, double v);

	//! add value to entry
	void add(int i, int j, double v);

	//! retrieve value
	double get(int i, int j);

	//! get the diagonal value
	double diag(int i);

	//! release memory for storing data
	void Clear();

	//! zero matrix elements
	void zero();

public:
	//! return number of blocks
	int Blocks() const { return (int) m_Block.size(); }

	//! get a block
	BLOCK& Block(int i, int j);

	//! find the partition index of an equation number i
	int find_partition(int i);

	//! return number of partitions
	int Partitions() const { return (int) m_part.size() - 1; }

	//! Start equation index of partition i
	int StartEquationIndex(int i) { return m_part[i]; }

	//! number of equations in partition i
	int PartitionEquations(int i) { return m_part[i+1]-m_part[i]; }

protected:
	vector<int>		m_part;		//!< partition list
	vector<BLOCK>	m_Block;	//!< block matrices
};
