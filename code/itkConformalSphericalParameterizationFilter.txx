#ifndef _itkConformalSphericalParameterizationFilter_txx
#define _itkConformalSphericalParameterizationFilter_txx

#include "itkConformalSphericalParameterizationFilter.h"
#include <deque>


namespace itk
{

  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::ConformalSphericalParameterizationFilter()
  {
    m_Sphere = 0;
    m_SourceMesh = 0;
    m_TutteStepLength = 0.1;
    m_TutteConvergence = 0.0001;
    m_HarmonicStepLength = 0.01;
    m_HarmonicConvergence = 0.00005;
    m_StepLength = 0;
    m_Convergence = 0;
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::~ConformalSphericalParameterizationFilter()
  {
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  void
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::GenerateData()
  {
    if (this->GetNumberOfInputs() < 1) 
    { 
      itkExceptionMacro( "Input mesh not set." ); 
    }

    // set m_SourceMesh and m_Sphere for fast access in all member functions
    m_SourceMesh = this->GetInput();
    ParameterizationPointer output = this->GetOutput();
    *output = (typename InputMeshType::ConstPointer)m_SourceMesh;
    output->InitializeSphericalMap();
    m_Sphere = output->GetSphericalMap();
    m_StringConstant.resize( m_SourceMesh->GetNumberOfEdges() );

    // initialize sphere with Gauss map
    this->ComputeGaussMap();

    // initialize variables for Tutte energy
    for (unsigned int edgeId=0; edgeId<m_SourceMesh->GetNumberOfEdges(); edgeId++)
    {
      m_StringConstant[edgeId] = 1.0;
    }
    m_StepLength = m_TutteStepLength;
    m_Convergence = m_TutteConvergence;
    // ...and minimze
    this->MinimizeEnergy();

    // initialize variables for Harmonic energy
    for (unsigned int edgeId=0; edgeId<m_SourceMesh->GetNumberOfEdges(); edgeId++)
    {
      m_StringConstant[edgeId] = 0.5 * ( this->GetCotangensAgainstEdge( edgeId, 0 ) + 
                                         this->GetCotangensAgainstEdge( edgeId, 1 ) );
      if (m_StringConstant[edgeId] < 0) {
        itkWarningMacro( "String Constant for edge " << edgeId << " is < 0!" );
        m_StringConstant[edgeId] = 0;
      }
    }
    m_StepLength = m_HarmonicStepLength;
    m_Convergence = m_HarmonicConvergence;
    // ...and minimze
    this->MinimizeEnergy();

    m_Sphere = 0;
    m_SourceMesh = 0;
    this->GetOutput()->SetBufferedRegion( 0 );
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  void
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::ComputeGaussMap()
  {
    itkDebugMacro( "Computing Gauss map..." );

    // init normal vectors for all triangles
    std::vector<VectorType> faceNormals;
    faceNormals.resize( m_SourceMesh->GetNumberOfFaces() );
    std::vector<bool> faceChecked, faceFlipped;
    faceChecked.resize( m_SourceMesh->GetNumberOfFaces() );
    faceFlipped.resize( m_SourceMesh->GetNumberOfFaces() );
    for (unsigned int faceId=0; faceId<m_SourceMesh->GetNumberOfFaces(); faceId++) 
    {
      faceChecked[faceId] = false;
      faceFlipped[faceId] = false;
      typename InputMeshType::IndexType vertexId[3];
      for (int i=0; i<3; i++) 
      { 
        vertexId[i] = m_SourceMesh->GetPointIndexForFace( faceId, i ); 
      }
      VectorType edge0Vec = m_SourceMesh->GetPoint( vertexId[1] ) -
                            m_SourceMesh->GetPoint( vertexId[0] );
      VectorType edge1Vec = m_SourceMesh->GetPoint( vertexId[2] ) - 
                            m_SourceMesh->GetPoint( vertexId[0] );
      vnl_vector_fixed<double, 3> v1, v2, res;
      v1 = edge0Vec.Get_vnl_vector();  
      v2 = edge1Vec.Get_vnl_vector();
      res = vnl_cross_3d( v1, v2 );
      faceNormals[faceId].Set_vnl_vector( res );
      faceNormals[faceId] /= faceNormals[faceId].GetNorm();
    }
    
    // determine vertex with largest distance from center:
    VectorType center;   center.Fill( 0 );
    for (unsigned long i=0; i<m_SourceMesh->GetNumberOfPoints(); i++) 
    { 
      center += m_SourceMesh->GetPoint( i ).GetVectorFromOrigin(); 
    }
    center /= ((double)m_SourceMesh->GetNumberOfPoints());
    unsigned long distantVertexId = 0;
    double maxDist = 0.0;
    for (unsigned long i=0; i<m_SourceMesh->GetNumberOfPoints(); i++) 
    {
      double dist = (m_SourceMesh->GetPoint( i ) - center).GetVectorFromOrigin().GetNorm();
      if (dist > maxDist) 
      {
        maxDist = dist;
        distantVertexId = i;
      }
    }

    // test if orientation of normal vector of the first adjacent face points in the right direction
    unsigned long distantFaceId = m_SourceMesh->GetFaceIndexForPoint( distantVertexId, 0 );
    if ( faceNormals[distantFaceId] * 
         (m_SourceMesh->GetPoint( distantVertexId ).GetVectorFromOrigin() - center ) < 0 ) 
    {
      faceNormals[distantFaceId] *= -1.0;
      faceFlipped[distantFaceId] = true;
    }
    faceChecked[distantFaceId] = true;
    std::deque<int> queue;
    queue.push_back( distantFaceId );

    while (!queue.empty()) 
    {
      int faceId = queue.front();
      queue.pop_front();
      for (int v=0; v<3; v++) 
      {
        int vertexId = m_SourceMesh->GetPointIndexForFace( faceId, v );
        int numLinks = m_SourceMesh->GetNumberOfFacesForPoint( vertexId );
        for (int f=0; f<numLinks; f++) 
        {
          int nbCand = m_SourceMesh->GetFaceIndexForPoint( vertexId, f );
          if (nbCand==faceId) continue;
          int vPos = -1;
          for (int nbv=0; nbv<3; nbv++) {
            if (m_SourceMesh->GetPointIndexForFace( nbCand, nbv ) == vertexId) 
            {
              vPos = nbv;
              break;
            }
          }
          assert( vPos != -1);
          if ( m_SourceMesh->GetPointIndexForFace( nbCand, (vPos-1+3)%3 ) == 
               m_SourceMesh->GetPointIndexForFace( faceId, (v+1)%3 ) ) 
          {
            if (!faceChecked[nbCand]) 
            {
              if (faceFlipped[faceId]) 
              {
                faceNormals[nbCand] *= -1.0;
                faceFlipped[nbCand] = true;
              }
              faceChecked[nbCand] = true;
              queue.push_back( nbCand );
            }
            break;
          }
          else if ( m_SourceMesh->GetPointIndexForFace( nbCand, (vPos+1)%3 ) == 
                    m_SourceMesh->GetPointIndexForFace( faceId, (v+1)%3 ) ) 
          {
            if (!faceChecked[nbCand]) 
            {
              if (!faceFlipped[faceId]) 
              {
                faceNormals[nbCand] *= -1.0;
                faceFlipped[nbCand] = true;
              }
              faceChecked[nbCand] = true;
              queue.push_back( nbCand );
            }
            break;
          }
        }
      }
    }
    for (unsigned int i=0; i<m_SourceMesh->GetNumberOfFaces(); i++) if (!faceChecked[i]) 
    {
      itkExceptionMacro( "Mesh consists of several parts!\n" );
    }

    // init normals for vertices:
    for (unsigned int vertexId=0; vertexId<m_SourceMesh->GetNumberOfPoints(); vertexId++) 
    {
      VectorType normal;
      normal.Fill( 0 );
      for (unsigned int faceCount=0; faceCount<m_SourceMesh->GetNumberOfFacesForPoint( vertexId ); faceCount++) 
      {
        normal += faceNormals[m_SourceMesh->GetFaceIndexForPoint( vertexId, faceCount )];
      }
      normal /= normal.GetNorm();
      (*m_Sphere)[vertexId].GetVnlVector().update( normal.GetVnlVector() ); 
    }

    // center and remap our Gauss map a couple of times for increased stability
    for (int i=0; i<100; i++) {
      this->CenterSphere();
      this->RemapToSphere();
    }
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  void
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::MinimizeEnergy()
  {
    double energy = this->GetEnergy();
    itkDebugMacro( "Initial energy = " << energy );
    // optimization loop
    std::vector<VectorType> derivative( m_Sphere->size() );
    unsigned int iterationCounter = 0;
    bool optimizationFinished = false;
    do {
      // compute absolute derivative for each vertex
      for (unsigned int i=0; i<m_Sphere->size(); i++) 
      {
        derivative[i] = this->GetAbsoluteDerivative( i );
      }
      // update vertices
      for (unsigned int i=0; i<m_Sphere->size(); i++) 
      {
        (*m_Sphere)[i] += derivative[i] * m_StepLength;
      }
      // center and remap all points
      this->CenterSphere();
      this->RemapToSphere();
      // get new energy and check for convergence
      double newEnergy = this->GetEnergy();
      double delta = energy - newEnergy;
      if (delta < m_Convergence) 
      {
        optimizationFinished = true;
      }
      energy = newEnergy;
      iterationCounter++;
    } while (!optimizationFinished);
    itkDebugMacro( "Energy after " << iterationCounter << " iterations = " << energy );
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  void
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::CenterSphere()
  {
    VectorType mean;
    mean.Fill( 0 );
    for (unsigned int i=0; i<m_Sphere->size(); i++) {
      mean += (*m_Sphere)[i].GetVectorFromOrigin();
    }
    mean /= m_Sphere->size();
    for (unsigned int i=0; i<m_Sphere->size(); i++) {
      (*m_Sphere)[i] -= mean;
    }
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  void
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::RemapToSphere()
  {
    PointType pnt;
    for (unsigned int i=0; i<m_Sphere->size(); i++) {
      VectorType v = (*m_Sphere)[i].GetVectorFromOrigin();
      v /= v.GetNorm();
      pnt.Fill( 0.0f );
      pnt += v;
      (*m_Sphere)[i] = pnt;
    }
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  typename ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>::VectorType 
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::GetAbsoluteDerivative( unsigned int pointId )
  {
    VectorType laplacian = this->GetPiecewiseLaplacian( pointId );
    // since all points lie on the unit sphere, the position is the normal at the same time:
    VectorType normalVec = (*m_Sphere)[pointId].GetVectorFromOrigin();
    VectorType laplacianNormal = normalVec * (laplacian * normalVec);
    return laplacian - laplacianNormal;
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  double
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::GetEnergy()
  {
    double sum = 0;
    for (unsigned int edgeId=0; edgeId<m_SourceMesh->GetNumberOfEdges(); edgeId++) {
      VectorType edge = (*m_Sphere)[m_SourceMesh->GetPointIndexForEdge( edgeId, 1 )] -
                        (*m_Sphere)[m_SourceMesh->GetPointIndexForEdge( edgeId, 0 )];
      sum += m_StringConstant[edgeId] * edge.GetNorm();
    }
    return sum;
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  typename ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>::VectorType 
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::GetPiecewiseLaplacian( unsigned int pointId )
  {
    VectorType sum;
    sum.Fill( 0 );
    for (unsigned int edgeCount=0; edgeCount<m_SourceMesh->GetNumberOfEdgesForPoint( pointId ); edgeCount++) {
      unsigned int edgeId = m_SourceMesh->GetEdgeIndexForPoint( pointId, edgeCount );
      VectorType edge = (*m_Sphere)[m_SourceMesh->GetPointIndexForEdge( edgeId, 1 )] -
                        (*m_Sphere)[m_SourceMesh->GetPointIndexForEdge( edgeId, 0 )];
      if (m_SourceMesh->GetPointIndexForEdge( edgeId, 0 ) != pointId) 
      { 
        edge *= -1;
      }
      sum += edge * m_StringConstant[edgeId];
    }
    return sum;
  }


  template <class TIndexedTriangleMesh, class TParameterizedTriangleMesh>
  double
  ConformalSphericalParameterizationFilter<TIndexedTriangleMesh, TParameterizedTriangleMesh>
  ::GetCotangensAgainstEdge( unsigned int edgeId, int localFaceId )
  {
    const PointType pt0 = m_SourceMesh->GetPoint( m_SourceMesh->GetPointIndexForEdge( edgeId, 0 ) );
    const PointType pt1 = m_SourceMesh->GetPoint( m_SourceMesh->GetPointIndexForEdge( edgeId, 1 ) );
    unsigned int missingId = m_SourceMesh->GetMissingPointIndex( edgeId, localFaceId );
    if (missingId < 0) {
      itkWarningMacro( "Mesh not closed at edge " << edgeId );
      return 0;
    }
    const PointType pt2 = m_SourceMesh->GetPoint( missingId );
    VectorType vec0 = pt0 - pt2;
    VectorType vec1 = pt1 - pt2;
    double crossLength = (vnl_cross_3d( vec0.Get_vnl_vector(), vec1.Get_vnl_vector() )).magnitude();
    return vec0 * vec1 / crossLength;
  }

}

#endif
