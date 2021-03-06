/*
	Copyright (c) 2012 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAT_WIREHAIR_HPP
#define CAT_WIREHAIR_HPP

/*
	Wirehair Streaming Forward Error Correction

		Wirehair is an FEC codec used to improve reliability of data sent
	over a Binary Erasure Channel (BEC) such as satellite Internet or WiFi.
	The data to transmit over the BEC is encoded into equal-sized blocks.
	When enough blocks are received at the other end of the channel, then
	the original data can be recovered.

	See WirehairDetails.hpp for more information.
*/

#include "WirehairDetails.hpp"

namespace cat {

namespace wirehair {


/*
	Wirehair Encoder

	Encodes message blocks for transmission over the network.
	The initialization function takes a while (say 10 milliseconds), so I
	recommend performing initialization in a separate thread to take advantage
	of modern multicore processors.

	(Block bytes) / (Milliseconds to initialize) = Throughput in MB/s (approx)

	Example usage pseudocode:

		cat::wirehair::Encoder encoder;

		encoder.Initialize(file_data, file_bytes, 1500);

		while (file not received on other end)
		{
			u8 buffer[1500];

			encoder.Generate(buffer);

			udp_send(buffer);
		}
*/
class Encoder
{
	// Check block state
	u32 _block_bytes;	// Number of bytes in a block
	u32 _final_bytes;	// Number of bytes in final block
	u16 _block_count;	// Number of blocks in the message
	u16 _added_count;	// Number of check blocks added
	u8 *_check_blocks;	// Pointer to start of check blocks
	u32 _g_seed;		// Seed for nonsingular generator matrix

	// Encoder state
	const u8 *_message_blocks;	// Original message data (final block is partial)
	u32 _next_block_id;			// Next block identifier to transmit
	u16 _block_next_prime;		// Next prime number including or higher than block count
	u16 _added_next_prime;		// Next prime number including or higher than added count

	// Peeling state
	struct PeelRow;
	struct PeelColumn;
	PeelRow *_peel_rows;		// Array of N peeling matrix rows
	PeelColumn *_peel_cols;		// Array of N peeling matrix columns

	// Lists
	static const u16 LIST_TERM = 0xffff;
	u16 _peel_head_rows;			// Head of peeling solved rows list
	u16 _defer_head_columns;		// Head of peeling deferred columns list
	u16 _defer_head_rows;			// Head of peeling deferred rows list
	u16 _defer_row_count;			// Count of deferred rows

	// Gaussian elimination state
	u64 *_ge_compress_matrix;	// Gaussian elimination compression matrix
	int _ge_compress_pitch;		// Pitch in words of GE compression matrix
	u64 *_ge_matrix;			// Gaussian elimination matrix
	int _ge_pitch;				// Pitch in words of GE matrix
	u16 *_ge_pivots;			// Pivots for each column of the GE matrix
	u16 *_ge_col_map;			// Map of GE columns to check matrix columns
	u16 *_ge_row_map;			// Map of GE rows to check matrix rows

	void PrintGECompressMatrix();
	void PrintGEMatrix();


	//// (1) Peeling

	// Avalanche peeling from the newly solved column to others
	void PeelAvalanche(u16 column_i, PeelColumn *column);

	// Peel a row using the given column
	void Peel(u16 row_i, PeelRow *row, u16 column_i);

	// Walk forward through rows and solve as many as possible before deferring any
	bool PeelSetup();

	// Greedy algorithm to select columns to defer and resume peeling until all columns are marked
	void GreedyPeeling();


	//// (2) Compression

	// Allocate matrices for compression operation and GE
	bool CompressAllocate();

	// Fill deferred rows of compress matrix
	void FillCompressDeferred();

	// Fill dense rows of compress matrix
	void FillCompressDense();

	// Fill deferred rows of GE matrix
	void FillGEDeferred();

	// Fill dense rows of GE matrix
	void FillGEDense();

	// Build GE matrix for compression
	bool CompressSetup();

	// Copy deferred columns to the GE matrix
	void CopyDeferredColumns();

	// Compress rectangular matrix into conceptual square matrix
	void Compress();


	//// (3) Gaussian Elimination

	// Triangularize the GE matrix (may fail if pivot cannot be found)
	bool Triangle();

	// Solve one column based on compress matrix and GE matrix
	void SolveTriangleColumn(u16 ge_row_i, u16 column_i, u16 pivot_i);

	// Solve pivot column values from the row op schedule from Triangle
	void SolveTriangleColumns();

	// Diagonalize the GE matrix to complete solving for the GE blocks
	void Diagonal();


	//// (4) Substitution

	// Substitute and solve for all of the peeled columns
	void Substitute();


	//// Misc

	// Main driver: Generate check blocks from message blocks
	bool GenerateCheckBlocks();

	// Free allocated memory
	void Cleanup();

public:
	Encoder();
	~Encoder();

	// Attempt to initialize the encoder for a given message
	// Returns false on initialization failure
	bool Initialize(const void *message_in, int message_bytes, int block_bytes);

	// Generate a block of size block_bytes specified during initialization
	void Generate(void *block);
};


/*
	Wirehair Decoder

	Decodes messages encoded by the Encoder above.
	The Initialize() function does not take much run time.
	The Decode() function will return true when decoding has completed.

	Example usage pseudocode:

		cat::wirehair::Decoder decoder;

		decoder.Initialize(out_file_buffer, file_bytes, 1500);

		do
		{
			u8 buffer[1500];

			udp_recv(buffer);

		} while (!decoder.Decode(buffer));
*/
class Decoder
{
public:
	// Attempt to initialize the decoder
	// Decoder will write to the given buffer once decoding completes
	// Returns false on initialization failure
	bool Initialize(void *message_out, int message_bytes, int block_bytes);

	// Decode the provided block of size block_bytes specified during initialization
	bool Decode(void *block);
};


} // namespace wirehair

} // namespace cat

#endif // CAT_WIREHAIR_HPP
