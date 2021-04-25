#include "Laplacian.h"
#include "MPIUtils.h"
#include "Function.h"
#include "DataFile.h"
#include "Vector.h"

#include <cmath>
#include <mpi.h>


Laplacian::Laplacian()
{
}


Laplacian::Laplacian(DataFile* DF, Function* function):
  _DF(DF), _function(function)
{
}


void Laplacian::Initialize(DataFile* DF, Function* function)
{
  _DF = DF;
  _function = function;
  this->Initialize();
}


void Laplacian::Initialize()
{
  // On récupère les paramètres necessaires pour construire la matrice
  double dx(_DF->getDx()), dy(_DF->getDy());
  double D(_DF->getDiffCoeff());
  double dt(_DF->getTimeStep());
  _Nx = _DF->getNx();
  _Ny = _DF->getNy();

  // Calcul des coefficients de la matrice
  // dt * A
  if (_DF->getTimeScheme() == "ExplicitEuler")
    {
      _alpha = dt * D / pow(dy,2);
      _beta = dt * D / pow(dx,2);
      _gamma = - 2. * dt * D * (1./pow(dx,2) + 1./pow(dy,2));
    }
  // I - dt * A
  else if (_DF->getTimeScheme() == "ImplicitEuler")
    {
      _alpha = - dt * D / pow(dy,2);
      _beta = - dt * D / pow(dx,2);
      _gamma = 1 + 2. * dt * D * (1./pow(dx,2) + 1./pow(dy,2));
    }
}


DVector Laplacian::matVecProd(const DVector& x)
{
  // Vecteur resultat
  DVector result;
  DVector prev, next;
  int size(x.size());
  result.resize(size, 0.);
  prev.resize(_Nx, 0.);
  next.resize(_Nx, 0.);

  // MPI Communications
  // Each proc has to communicate with proc - 1 and proc + 1
  if (MPI_Rank + 1 < MPI_Size)
    {
      MPI_Sendrecv(&x[size - _Nx], _Nx, MPI_DOUBLE, MPI_Rank + 1, 0,
                   &next[0], _Nx, MPI_DOUBLE, MPI_Rank + 1, 1,
                   MPI_COMM_WORLD, &status);
    }
  if (MPI_Rank - 1 >= 0)
    {
      MPI_Sendrecv(&x[0], _Nx, MPI_DOUBLE, MPI_Rank - 1, 1,
                   &prev[0], _Nx, MPI_DOUBLE, MPI_Rank - 1, 0,
                   MPI_COMM_WORLD, &status);
    }
  
  // Boucle
  for (int k(0) ; k < size ; ++k)
    {
      // Indices
      int i(k%_Nx), j(k/_Nx);

      // Termes diagonaux
      result[k] += _gamma * x[k];

      // Termes non diagonaux
      if (j == 0)
        result[k] += _alpha * prev[i];
      else
        result[k] += _alpha * x[k-_Nx];

      if (i != 0)
        result[k] += _beta * x[k-1];
      if (i != _Nx - 1)
        result[k] += _beta * x[k+1];

      if (j == nbDomainRows - 1)
        result[k] += _alpha * next[i];
      else
        result[k] += _alpha * x[k+_Nx];
    }

  return result;
}


DVector Laplacian::solveConjGrad(const DVector& b, const DVector& x0, double tolerance, int maxIterations, std::ofstream& resFile)
{
  // Variables intermédiaires
  DVector x(x0);
  DVector res(b - this->matVecProd(x0));
  DVector p(res);
  // Compute initial global residual
  double resDotRes(0.), partialResDotRes(res.dot(res));
  MPI_Allreduce(&partialResDotRes, &resDotRes, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double beta(sqrt(resDotRes));
  if (MPI_Rank == 0)
    resFile << beta << std::endl;
  
  // Itérations de la méthode
  int k(0);
  while ((beta > tolerance) && (k < maxIterations))
    {
      // Produit matvec
      DVector z(this->matVecProd(p));
      // Dot products
      double resDotP(0.), zDotP(0.);
      double partialResDotP(res.dot(p)), partialZDotP(z.dot(p));
      MPI_Allreduce(&partialResDotP, &resDotP, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      MPI_Allreduce(&partialZDotP, &zDotP, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      // COmpute alpha
      double alpha(resDotP/zDotP);
      // Update the solution
      x = x + alpha * p;
      // Update the residual
      res = res - alpha * z;
      // Dot product
      double resDotRes(0.), partialResDotRes(res.dot(res));
      MPI_Allreduce(&partialResDotRes, &resDotRes, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      // Compute gamma
      double gamma(resDotRes/pow(beta,2));
      // Update p
      p = res + gamma * p;
      beta = sqrt(resDotRes);
      ++k;
      if (MPI_Rank == 0)
        resFile << beta << std::endl;
    }
  
  // Logs
#if VERBOSITY>1
  if (MPI_Rank == 0)
    {
      if ((k == maxIterations) && (beta > tolerance))
        {
          std::cout << termcolor::yellow << "SOLVER::GC::WARNING : The GC method did not converge. Residual L2 norm = " << beta << " (" << maxIterations << " iterations)" << std::endl;
          std::cout << termcolor::reset;
        }
      else
        {
          std::cout << termcolor::green << "SOLVER::GC::SUCCESS : The GC method converged in " << k << " iterations ! Residual L2 norm = " << beta << std::endl;
          std::cout << termcolor::reset;
        } 
    }
#endif
  
  return x;
}
