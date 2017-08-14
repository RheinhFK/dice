// @HEADER
// ************************************************************************
//
//               Digital Image Correlation Engine (DICe)
//                 Copyright 2015 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact: Dan Turner (dzturne@sandia.gov)
//
// ************************************************************************
// @HEADER

#include <DICe_LocalShapeFunction.h>
#include <DICe_MeshEnums.h>
#include <DICe_Schema.h>

namespace DICe {

using namespace DICe::mesh::field_enums;

DICE_LIB_DLL_EXPORT
Teuchos::RCP<Local_Shape_Function> shape_function_factory(Schema * schema){
  if(schema->affine_matrix_enabled()){
    TEUCHOS_TEST_FOR_EXCEPTION(true,std::runtime_error,"Error, this method is not ready yet, yo!");
  }else{
    return Teuchos::rcp(new Affine_Shape_Function(schema->rotation_enabled(),schema->normal_strain_enabled(),schema->shear_strain_enabled()));
  }
}

void
Local_Shape_Function::save_fields(Schema * schema,
  const int_t subset_gid){
  // iterate the fields and save off the values
  std::map<Field_Spec,size_t>::const_iterator it = spec_map_.begin();
  const std::map<Field_Spec,size_t>::const_iterator it_end = spec_map_.end();
  for(;it!=it_end;++it)
    schema->global_field_value(subset_gid,it->first) = parameters_[it->second];
}

void
Local_Shape_Function::create_fields(Schema * schema){
  // iterate the field specs and create the necessary fields
  std::map<Field_Spec,size_t>::const_iterator it = spec_map_.begin();
  const std::map<Field_Spec,size_t>::const_iterator it_end = spec_map_.end();
  for(;it!=it_end;++it){
    schema->mesh()->create_field(it->first);
    // creat the nm1 field as well
    DICe::mesh::field_enums::Field_Spec fs_nm1(it->first.get_field_type(),it->first.get_name(),it->first.get_rank(),DICe::mesh::field_enums::STATE_N_MINUS_ONE,false,true);
    schema->mesh()->create_field(fs_nm1);
  }
}

void
Affine_Shape_Function::reset_fields(Schema * schema){
  // iterate the fields and save off the values
  std::map<Field_Spec,size_t>::const_iterator it = spec_map_.begin();
  const std::map<Field_Spec,size_t>::const_iterator it_end = spec_map_.end();
  for(;it!=it_end;++it)
    schema->mesh()->get_field(it->first)->put_scalar(0.0);
}

Affine_Shape_Function::Affine_Shape_Function(const bool enable_rotation,
  const bool enable_normal_strain,
  const bool enable_shear_strain):
  Local_Shape_Function()
{
  // translation is required
  spec_map_.insert(std::pair<Field_Spec,size_t>(SUBSET_DISPLACEMENT_X_FS,spec_map_.size()));
  spec_map_.insert(std::pair<Field_Spec,size_t>(SUBSET_DISPLACEMENT_Y_FS,spec_map_.size()));
  //if(enable_rotation)
  spec_map_.insert(std::pair<Field_Spec,size_t>(ROTATION_Z_FS,spec_map_.size()));
  //if(enable_normal_strain){
  spec_map_.insert(std::pair<Field_Spec,size_t>(NORMAL_STRETCH_XX_FS,spec_map_.size()));
  spec_map_.insert(std::pair<Field_Spec,size_t>(NORMAL_STRETCH_YY_FS,spec_map_.size()));
  //}
  //if(enable_shear_strain)
  spec_map_.insert(std::pair<Field_Spec,size_t>(SHEAR_STRETCH_XY_FS,spec_map_.size()));
  num_params_ = spec_map_.size();
  assert(num_params_==DICE_DEFORMATION_SIZE);
  parameters_.resize(num_params_);
  // initialize the parameter values
  clear();
}

void
Affine_Shape_Function::map(const scalar_t & x,
  const scalar_t & y,
  const scalar_t & cx,
  const scalar_t & cy,
  scalar_t & out_x,
  scalar_t & out_y){

  static scalar_t dx=0.0,dy=0.0;
  static scalar_t Dx=0.0,Dy=0.0;
  static scalar_t cost = std::cos(parameter(ROTATION_Z_FS));
  static scalar_t sint = std::sin(parameter(ROTATION_Z_FS));
  dx = x - cx;
  dy = y - cy;
  Dx = (1.0+parameter(NORMAL_STRETCH_XX_FS))*dx + parameter(SHEAR_STRETCH_XY_FS)*dy;
  Dy = (1.0+parameter(NORMAL_STRETCH_YY_FS))*dy + parameter(SHEAR_STRETCH_XY_FS)*dx;
  // mapped location
  out_x = cost*Dx - sint*Dy + parameter(SUBSET_DISPLACEMENT_X_FS) + cx;
  out_y = sint*Dx + cost*Dy + parameter(SUBSET_DISPLACEMENT_Y_FS) + cy;
}


void
Affine_Shape_Function::clear(){
  for(int_t i=0;i<num_params_;++i)
    parameters_[i] = 0.0;
}

void
Affine_Shape_Function::initialize_parameters_from_fields(Schema * schema,
  const int_t subset_gid){
  const Projection_Method projection = schema->projection_method();
  if(schema->translation_enabled()){
    DEBUG_MSG("Subset " << subset_gid << " Translation is enabled.");
    if(schema->frame_id() > schema->first_frame_id()+2 && projection == VELOCITY_BASED){
      parameter(SUBSET_DISPLACEMENT_X_FS) = schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_X_FS) +
          (schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_X_FS)-schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_X_NM1_FS));
      parameter(SUBSET_DISPLACEMENT_Y_FS) = schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_Y_FS) +
          (schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_Y_FS)-schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_Y_NM1_FS));

    }
    else{
      parameter(SUBSET_DISPLACEMENT_X_FS) = schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_X_FS);
      parameter(SUBSET_DISPLACEMENT_Y_FS) = schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_Y_FS);
    }
  }
  if(schema->rotation_enabled()){
    DEBUG_MSG("Subset " << subset_gid << " Rotation is enabled.");
    if(schema->frame_id() > schema->first_frame_id()+2 && projection == VELOCITY_BASED){
      parameter(ROTATION_Z_FS) = schema->global_field_value(subset_gid,ROTATION_Z_FS) +
          (schema->global_field_value(subset_gid,ROTATION_Z_FS)-schema->global_field_value(subset_gid,ROTATION_Z_NM1_FS));
    }
    else{
      parameter(ROTATION_Z_FS) = schema->global_field_value(subset_gid,ROTATION_Z_FS);
    }
  }
  if(schema->normal_strain_enabled()){
    DEBUG_MSG("Subset " << subset_gid << " Normal strain is enabled.");
    parameter(NORMAL_STRETCH_XX_FS) = schema->global_field_value(subset_gid,NORMAL_STRETCH_XX_FS);
    parameter(NORMAL_STRETCH_YY_FS) = schema->global_field_value(subset_gid,NORMAL_STRETCH_YY_FS);
  }
  if(schema->shear_strain_enabled()){
    DEBUG_MSG("Subset " << subset_gid << " Shear strain is enabled.");
    parameter(SHEAR_STRETCH_XY_FS) = schema->global_field_value(subset_gid,SHEAR_STRETCH_XY_FS);
  }
//  if(sid!=subset_gid)
//    DEBUG_MSG("Subset " << subset_gid << " was initialized from the field values of subset " << sid);
//  else{
//    DEBUG_MSG("Projection Method: " << projection);
//    DEBUG_MSG("Subset " << subset_gid << " solution from prev. step: u " << schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_X_FS) <<
//      " v " << schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_Y_FS) <<
//      " theta " << schema->global_field_value(subset_gid,ROTATION_Z_FS) <<
//      " e_x " << schema->global_field_value(subset_gid,NORMAL_STRETCH_XX_FS) <<
//      " e_y " << schema->global_field_value(subset_gid,NORMAL_STRETCH_YY_FS) <<
//      " g_xy " << schema->global_field_value(subset_gid,SHEAR_STRETCH_XY_FS));
//    DEBUG_MSG("Subset " << subset_gid << " solution from nm1 step: u " << schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_X_NM1_FS) <<
//      " v " << schema->global_field_value(subset_gid,SUBSET_DISPLACEMENT_Y_NM1_FS) <<
//      " theta " << schema->global_field_value(subset_gid,ROTATION_Z_NM1_FS) <<
//      " e_x " << schema->global_field_value(subset_gid,NORMAL_STRETCH_XX_NM1_FS) <<
//      " e_y " << schema->global_field_value(subset_gid,NORMAL_STRETCH_YY_NM1_FS) <<
//      " g_xy " << schema->global_field_value(subset_gid,SHEAR_STRETCH_XY_NM1_FS));
//  }
  DEBUG_MSG("Subset initialized from subset gid " << subset_gid << " with values: u " << parameter(SUBSET_DISPLACEMENT_X_FS) <<
    " v " << parameter(SUBSET_DISPLACEMENT_Y_FS) <<
    " theta " << parameter(ROTATION_Z_FS) <<
    " e_x " << parameter(NORMAL_STRETCH_XX_FS) <<
    " e_y " << parameter(NORMAL_STRETCH_YY_FS) <<
    " g_xy " << parameter(SHEAR_STRETCH_XY_FS));
}

void
Affine_Shape_Function::add_translation(const scalar_t & u,
  const scalar_t & v){
  // zeroth entry is always u
  parameter(SUBSET_DISPLACEMENT_X_FS) += u;
  // id one corresponds to v
  parameter(SUBSET_DISPLACEMENT_Y_FS) += v;
}

void
Affine_Shape_Function::insert_motion(const scalar_t & u,
  const scalar_t & v,
  const scalar_t & theta){
  parameter(SUBSET_DISPLACEMENT_X_FS) = u;
  parameter(SUBSET_DISPLACEMENT_Y_FS) = v;
  parameter(ROTATION_Z_FS) = theta;
}

void
Affine_Shape_Function::insert_motion(const scalar_t & u,
  const scalar_t & v){
  parameter(SUBSET_DISPLACEMENT_X_FS) = u;
  parameter(SUBSET_DISPLACEMENT_Y_FS) = v;
}

void
Affine_Shape_Function::map_to_u_v_theta(const scalar_t & x,
  const scalar_t & y,
  scalar_t & out_u,
  scalar_t & out_v,
  scalar_t & out_theta){
  out_u = parameter(SUBSET_DISPLACEMENT_X_FS);
  out_v = parameter(SUBSET_DISPLACEMENT_Y_FS);
  out_theta = spec_map_.find(ROTATION_Z_FS)!=spec_map_.end() ? parameter(ROTATION_Z_FS) : 0.0;
}

// TODO for Affine map, remember to create a virtual function save_fields that calls Local_Shape_Function::save_fields() then sets rotation and displacement as is done below

//global_field_value(subset_gid,AFFINE_A_FS) = (*deformation)[DOF_A];
//global_field_value(subset_gid,AFFINE_B_FS) = (*deformation)[DOF_B];
//global_field_value(subset_gid,AFFINE_C_FS) = (*deformation)[DOF_C];
//global_field_value(subset_gid,AFFINE_D_FS) = (*deformation)[DOF_D];
//global_field_value(subset_gid,AFFINE_E_FS) = (*deformation)[DOF_E];
//global_field_value(subset_gid,AFFINE_F_FS) = (*deformation)[DOF_F];
//global_field_value(subset_gid,AFFINE_G_FS) = (*deformation)[DOF_G];
//global_field_value(subset_gid,AFFINE_H_FS) = (*deformation)[DOF_H];
//global_field_value(subset_gid,AFFINE_I_FS) = (*deformation)[DOF_I];
//const scalar_t x = global_field_value(subset_gid,SUBSET_COORDINATES_X_FS);
//const scalar_t y = global_field_value(subset_gid,SUBSET_COORDINATES_Y_FS);
//scalar_t u = 0.0,v=0.0,theta=0.0;
//affine_map_to_motion(x,y,u,v,theta,deformation);
//global_field_value(subset_gid,SUBSET_DISPLACEMENT_X_FS) = u;
//global_field_value(subset_gid,SUBSET_DISPLACEMENT_Y_FS) = v;
//global_field_value(subset_gid,ROTATION_Z_FS) = theta;


}// End DICe Namespace
