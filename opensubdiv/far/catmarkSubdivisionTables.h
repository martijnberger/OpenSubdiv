//
//   Copyright 2013 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#ifndef FAR_CATMARK_SUBDIVISION_TABLES_H
#define FAR_CATMARK_SUBDIVISION_TABLES_H

#include "../version.h"

#include "../far/subdivisionTables.h"

#include <cassert>
#include <vector>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

/// \brief Catmark subdivision scheme tables.
///
/// Catmull-Clark tables store the indexing tables required in order to compute
/// the refined positions of a mesh without the help of a hierarchical data
/// structure. The advantage of this representation is its ability to be executed
/// in a massively parallel environment without data dependencies.
///
class FarCatmarkSubdivisionTables : public FarSubdivisionTables {

public:

    /// \brief Returns the number of indexing tables needed to represent this
    /// particular subdivision scheme.
    virtual int GetNumTables() const { return 7; }

    /// \brief  Returns the subdivision scheme of the tables 
    virtual FarSubdivisionTables::Scheme GetScheme() const { 
        return FarSubdivisionTables::CATMARK; 
    }

private:
    template <class X, class Y> friend class FarCatmarkSubdivisionTablesFactory;
    template <class X, class Y> friend class FarMultiMeshFactory;
    friend class FarComputeController;

    // Private constructor called by factory
    FarCatmarkSubdivisionTables( int maxlevel );

    // Compute-kernel applied to vertices resulting from the refinement of a face.
    template <class U>
    void computeFacePoints(int offset, int level, int start, int end, U * vsrc) const;

    // Compute-kernel applied to vertices resulting from the refinement of an edge.
    template <class U>
    void computeEdgePoints(int offset, int level, int start, int end, U * vsrc) const;

    // Compute-kernel applied to vertices resulting from the refinement of a vertex
    // Kernel "A" Handles the k_Smooth and k_Dart rules
    template <class U>
    void computeVertexPointsA(int offset, bool pass, int level, int start, int end, U * vsrc) const;

    // Compute-kernel applied to vertices resulting from the refinement of a vertex
    // Kernel "B" Handles the k_Crease and k_Corner rules
    template <class U>
    void computeVertexPointsB(int offset, int level, int start, int end, U * vsrc) const;

};

inline
FarCatmarkSubdivisionTables::FarCatmarkSubdivisionTables( int maxlevel ) :
    FarSubdivisionTables(maxlevel) {
}

//
// Face-vertices compute Kernel - completely re-entrant
//

template <class U> void
FarCatmarkSubdivisionTables::computeFacePoints( int offset, int tableOffset, int start, int end, U * vsrc ) const {

    U * vdst = vsrc + offset + start;

    for (int i=start+tableOffset; i<end+tableOffset; ++i, ++vdst ) {

        vdst->Clear();

        int h = this->_F_ITa[2*i  ],
            n = this->_F_ITa[2*i+1];
        float weight = 1.0f/n;

        for (int j=0; j<n; ++j) {
             vdst->AddWithWeight( vsrc[ this->_F_IT[h+j] ], weight );
             vdst->AddVaryingWithWeight( vsrc[ this->_F_IT[h+j] ], weight );
        }
    }
}

//
// Edge-vertices compute Kernel - completely re-entrant
//

template <class U> void
FarCatmarkSubdivisionTables::computeEdgePoints( int offset, int tableOffset, int start, int end, U * vsrc ) const {

    U * vdst = vsrc + offset + start;

    for (int i=start+tableOffset; i<end+tableOffset; ++i, ++vdst ) {

        vdst->Clear();

        int eidx0 = this->_E_IT[4*i+0],
            eidx1 = this->_E_IT[4*i+1],
            eidx2 = this->_E_IT[4*i+2],
            eidx3 = this->_E_IT[4*i+3];

        float vertWeight = this->_E_W[i*2+0];

        // Fully sharp edge : vertWeight = 0.5f
        vdst->AddWithWeight( vsrc[eidx0], vertWeight );
        vdst->AddWithWeight( vsrc[eidx1], vertWeight );

        if (eidx2!=-1) {
            // Apply fractional sharpness
            float faceWeight = this->_E_W[i*2+1];

            vdst->AddWithWeight( vsrc[eidx2], faceWeight );
            vdst->AddWithWeight( vsrc[eidx3], faceWeight );
        }

        vdst->AddVaryingWithWeight( vsrc[eidx0], 0.5f );
        vdst->AddVaryingWithWeight( vsrc[eidx1], 0.5f );
    }
}

//
// Vertex-vertices compute Kernels "A" and "B" - completely re-entrant
//

// multi-pass kernel handling k_Crease and k_Corner rules
template <class U> void
FarCatmarkSubdivisionTables::computeVertexPointsA( int offset, bool pass, int tableOffset, int start, int end, U * vsrc ) const {

    U * vdst = vsrc + offset + start;

    for (int i=start+tableOffset; i<end+tableOffset; ++i, ++vdst ) {

        if (not pass)
            vdst->Clear();

        int     n=this->_V_ITa[5*i+1],   // number of vertices in the _VO_IT array (valence)
                p=this->_V_ITa[5*i+2],   // index of the parent vertex
            eidx0=this->_V_ITa[5*i+3],   // index of the first crease rule edge
            eidx1=this->_V_ITa[5*i+4];   // index of the second crease rule edge

        float weight = pass ? this->_V_W[i] : 1.0f - this->_V_W[i];

        // In the case of fractional weight, the weight must be inverted since
        // the value is shared with the k_Smooth kernel (statistically the
        // k_Smooth kernel runs much more often than this one)
        if (weight>0.0f and weight<1.0f and n>0)
            weight=1.0f-weight;

        // In the case of a k_Corner / k_Crease combination, the edge indices
        // won't be null,  so we use a -1 valence to detect that particular case
        if (eidx0==-1 or (pass==false and (n==-1)) ) {
            // k_Corner case
            vdst->AddWithWeight( vsrc[p], weight );
        } else {
            // k_Crease case
            vdst->AddWithWeight( vsrc[p], weight * 0.75f );
            vdst->AddWithWeight( vsrc[eidx0], weight * 0.125f );
            vdst->AddWithWeight( vsrc[eidx1], weight * 0.125f );
        }
        vdst->AddVaryingWithWeight( vsrc[p], 1.0f );
    }
}

// multi-pass kernel handling k_Dart and k_Smooth rules
template <class U> void
FarCatmarkSubdivisionTables::computeVertexPointsB( int offset, int tableOffset, int start, int end, U * vsrc ) const {

    U * vdst = vsrc + offset + start;

    for (int i=start+tableOffset; i<end+tableOffset; ++i, ++vdst ) {

        vdst->Clear();

        int h = this->_V_ITa[5*i  ],     // offset of the vertices in the _V0_IT array
            n = this->_V_ITa[5*i+1],     // number of vertices in the _VO_IT array (valence)
            p = this->_V_ITa[5*i+2];     // index of the parent vertex

        float weight = this->_V_W[i],
                  wp = 1.0f/(n*n),
                  wv = (n-2.0f)*n*wp;

        vdst->AddWithWeight( vsrc[p], weight * wv );

        for (int j=0; j<n; ++j) {
            vdst->AddWithWeight( vsrc[this->_V_IT[h+j*2  ]], weight * wp );
            vdst->AddWithWeight( vsrc[this->_V_IT[h+j*2+1]], weight * wp );
        }
        vdst->AddVaryingWithWeight( vsrc[p], 1.0f );
    }
}

} // end namespace OPENSUBDIV_VERSION
using namespace OPENSUBDIV_VERSION;

} // end namespace OpenSubdiv

#endif /* FAR_CATMARK_SUBDIVISION_TABLES_H */
