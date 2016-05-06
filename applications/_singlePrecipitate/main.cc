// Beta prime precipitate evolution implementation
// Code to calculate the steady-state morphology of a single precipitate
//general headers
#include "../../include/dealIIheaders.h"

//Coupled Cahn-Hilliard+Allen-Cahn+Mechanics problem headers
//#include "parameters_bPPE.h"
#include "parameters.h"
#include "../../src/models/coupled/coupledCHACMechanics.h"

//initial condition for concentration
template <int dim>
class InitialConditionC : public Function<dim>
{
public:
  double shift;
  InitialConditionC (double _shift) : Function<dim>(1), shift(_shift) {
    std::srand(Utilities::MPI::this_mpi_process(MPI_COMM_WORLD)+1);
  }
  double value (const Point<dim> &p, const unsigned int component = 0) const
  {
	  //set result equal to the structural order parameter initial condition
	  double dx=spanX/( (double)subdivisionsX )/std::pow(2.0,refineFactor);
	  double dy=spanY/( (double)subdivisionsY )/std::pow(2.0,refineFactor);
	  double dz=spanZ/( (double)subdivisionsZ )/std::pow(2.0,refineFactor);
	  double r=0.0;
	  std::vector<double> ellipsoid_denoms;
	  ellipsoid_denoms.push_back(x_denom);
	  ellipsoid_denoms.push_back(y_denom);
	  ellipsoid_denoms.push_back(z_denom);


	  for (unsigned int i=0; i<dim; i++){
		  r += (p.operator()(i))*(p.operator()(i))/ellipsoid_denoms[i];
	  }
	  r = sqrt(r);
	  return 0.5*(c_precip-c_matrix)*(1.0-std::tanh((r-initial_radius)/(initial_interface_coeff))) + c_matrix + shift;

  }
};

//initial condition for the structural order parameters
template <int dim>
class InitialConditionN : public Function<dim>
{
public:
  unsigned int index;
  InitialConditionN (const unsigned int _index) : Function<dim>(1), index(_index) {
    std::srand(Utilities::MPI::this_mpi_process(MPI_COMM_WORLD)+1);
  }
  double value (const Point<dim> &p, const unsigned int component = 0) const
  {
	  //set result equal to the structural order parameter initial condition
	  double dx=spanX/( (double)subdivisionsX )/std::pow(2.0,refineFactor);
	  double dy=spanY/( (double)subdivisionsY )/std::pow(2.0,refineFactor);
	  double dz=spanZ/( (double)subdivisionsZ )/std::pow(2.0,refineFactor);
	  double r=0.0;
	  std::vector<double> ellipsoid_denoms;
	  ellipsoid_denoms.push_back(x_denom);
	  ellipsoid_denoms.push_back(y_denom);
	  ellipsoid_denoms.push_back(z_denom);

	  if (index==1){
		  for (unsigned int i=0; i<dim; i++){
			  r += (p.operator()(i))*(p.operator()(i))/ellipsoid_denoms[i];
		  }
		  r = sqrt(r);
		  return 0.5*(1.0-std::tanh((r-initial_radius)/(initial_interface_coeff)));
	  }
	  else if (index==2){
		  return 0.0;
	  }
	  else{
		  return 0.0;
	  }

	  Assert (index <= 3, ExcMessage("An exception occurred. Index for structural order parameter must be 3 or below."));
  }

};

//apply initial conditions
template <int dim>
void CoupledCHACMechanicsProblem<dim>::applyInitialConditions()
{

  unsigned int fieldIndex;
  //call initial condition function for c
  fieldIndex=this->getFieldIndex("c");
  VectorTools::interpolate (*this->dofHandlersSet[fieldIndex], InitialConditionC<dim>(0.0), *this->solutionSet[fieldIndex]);
  //call initial condition function for structural order parameters
  fieldIndex=this->getFieldIndex("n1");
  VectorTools::interpolate (*this->dofHandlersSet[fieldIndex], InitialConditionN<dim>(1), *this->solutionSet[fieldIndex]);
  if (num_sop > 1){
	  fieldIndex=this->getFieldIndex("n2");
	  VectorTools::interpolate (*this->dofHandlersSet[fieldIndex], InitialConditionN<dim>(2), *this->solutionSet[fieldIndex]);
	  if (num_sop > 2){
		  fieldIndex=this->getFieldIndex("n3");
		  VectorTools::interpolate (*this->dofHandlersSet[fieldIndex], InitialConditionN<dim>(3), *this->solutionSet[fieldIndex]);
	  }
  }

  //set zero intial condition for u
  fieldIndex=this->getFieldIndex("u");
  *this->solutionSet[fieldIndex]=0.0;

}

//apply Dirchlet BC function
template <int dim>
void CoupledCHACMechanicsProblem<dim>::applyDirichletBCs(){
  //Set u=0 at all boundaries

	// Set all components of u to zero at the "external boundaries" (i.e. where none of x, y, or z are zero)
	VectorTools::interpolate_boundary_values (*this->dofHandlersSet[this->getFieldIndex("u")],\
	  					    0, ZeroFunction<dim>(dim), *(ConstraintMatrix*) \
	  					    this->constraintsSet[this->getFieldIndex("u")]);

	// Set only the normal component of u to zero at the "internal boundaries" (i.e. where one of x, y, or z are zero)
	std::vector<bool> component_mask;

	for (unsigned int direction=1; direction<dim+1; direction++){
		if (direction == 1){ component_mask.push_back(true); }
		else{ component_mask.push_back(false); }

		if (dim > 1){
			if (direction == 2){ component_mask.push_back(true); }
			else{ component_mask.push_back(false); }
		}

		if (dim > 2){
			if (direction == 3){ component_mask.push_back(true); }
			else{ component_mask.push_back(false); }
		}

		dealii::ComponentMask BC_mask(component_mask);

		VectorTools::interpolate_boundary_values (*this->dofHandlersSet[this->getFieldIndex("u")],\
					direction, ZeroFunction<dim>(dim), *(ConstraintMatrix*) \
					this->constraintsSet[this->getFieldIndex("u")],BC_mask);

		component_mask.clear();
	}
}

// Shift the initial concentration so that the average concentration is the desired value
template <int dim>
void CoupledCHACMechanicsProblem<dim>::shiftConcentration()
{
	unsigned int fieldIndex;
	fieldIndex=this->getFieldIndex("c");

	double integrated_concentration;
	computeIntegral(integrated_concentration);

	double volume = spanX;
	if (dim > 1) {
		volume *= spanY;
		if (dim > 2) {
			volume *= spanZ;
		}
	}

	double shift = c_avg - integrated_concentration/volume;

	if (Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0){
		std::cout<<"Matrix concentration shifted from " <<c_matrix<<" to " << c_matrix+shift <<std::endl;
	}

	try{
		if (shift + c_matrix < 0.0) {throw 0;}
	}
	catch (int e){
		Assert (shift > c_matrix, ExcMessage("An exception occurred. Initial concentration was shifted below zero."));
	}

	*this->solutionSet[fieldIndex]=0.0;
	VectorTools::interpolate (*this->dofHandlersSet[fieldIndex], InitialConditionC<dim>(shift), *this->solutionSet[fieldIndex]);
	MatrixFreePDE<dim>::solutionSet[fieldIndex]->update_ghost_values();

	computeIntegral(integrated_concentration);
}

//methods to mark boundaries
template <int dim>
void CoupledCHACMechanicsProblem<dim>::markBoundaries(){

	// Mark the x=0, y=0, and z=0 faces with 1, 2, and 3, respectively. The x=spanX, y=spanY, and z=spanZ faces are left at the default value (0)

	std::vector<double> domain_size;
	domain_size.push_back(spanX);
	domain_size.push_back(spanY);
	domain_size.push_back(spanZ);

	typename Triangulation<dim>::cell_iterator
	cell = MatrixFreePDE<dim>::triangulation.begin (),
	endc = MatrixFreePDE<dim>::triangulation.end();

	for (; cell!=endc; ++cell){

		// Mark one or more faces
		for (unsigned int face_number=0; face_number<GeometryInfo<dim>::faces_per_cell;++face_number){
			for (unsigned int i=0; i<dim; i++){
				//Mark both the x/y/z=0 face and the x/y/z=span face
				//if ((std::fabs(cell->face(face_number)->center()(i) - (0)) < 1e-12)||(std::fabs(cell->face(face_number)->center()(i) - (domain_size[i])) < 1e-12) ){

				//Mark only the x/y/z=0 face
				if ( std::fabs(cell->face(face_number)->center()(i) - (0)) < 1e-12 ){
				cell->face(face_number)->set_boundary_indicator (i+1);
				}

			}
		}
	}
}

//main
int main (int argc, char **argv)
{
  Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv,numbers::invalid_unsigned_int);
  try
    {
      deallog.depth_console(0);
      CoupledCHACMechanicsProblem<problemDIM> problem;
      problem.fields.push_back(Field<problemDIM>(SCALAR, PARABOLIC, "c"));
      problem.fields.push_back(Field<problemDIM>(SCALAR, PARABOLIC, "n1"));
      if (num_sop > 1){
    	  problem.fields.push_back(Field<problemDIM>(SCALAR, PARABOLIC, "n2"));
    	  if (num_sop > 2){
    		  problem.fields.push_back(Field<problemDIM>(SCALAR, PARABOLIC, "n3"));
    	  }
      }
      problem.fields.push_back(Field<problemDIM>(VECTOR,  ELLIPTIC, "u"));
      problem.init ();
      if (adjust_avg_c){
    	  problem.shiftConcentration();
      }
      problem.solve();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }

  return 0;
}

