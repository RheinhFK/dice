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

#include <DICe_Subset.h>
#include <DICe_ImageIO.h>
#if DICE_KOKKOS
  #include <DICe_Kokkos.h>
#endif

#include <cassert>

namespace DICe {

DICE_LIB_DLL_EXPORT
void affine_map_to_motion( const scalar_t & x,
  const scalar_t & y,
  scalar_t & out_u,
  scalar_t & out_v,
  scalar_t & out_theta,
  const Teuchos::RCP<const std::vector<scalar_t> > & def){
  if(def->size()==DICE_DEFORMATION_SIZE){
    out_u = (*def)[DOF_U];
    out_v = (*def)[DOF_V];
    out_theta = (*def)[DOF_THETA];
  }else if(def->size()==DICE_DEFORMATION_SIZE_AFFINE){
    scalar_t x_prime = 0.0;
    scalar_t y_prime = 0.0;
    map_affine(x,y,x_prime,y_prime,def);
    out_u = x_prime - x;
    out_v = y_prime - y;
    // estimate the rotation using the atan2 function (TODO this could be improved)
    out_theta = std::atan2((*def)[DOF_B],(*def)[DOF_A]);
  }
}

//DICE_LIB_DLL_EXPORT
//void affine_add_translation( const scalar_t & u,
//  const scalar_t & v,
//  Teuchos::RCP<std::vector<scalar_t> > & def){
//  assert(def->size()==DICE_DEFORMATION_SIZE_AFFINE);
//  const scalar_t A = (*def)[DOF_A];
//  const scalar_t B = (*def)[DOF_B];
//  const scalar_t C = (*def)[DOF_C];
//  const scalar_t D = (*def)[DOF_D];
//  const scalar_t E = (*def)[DOF_E];
//  const scalar_t F = (*def)[DOF_F];
//  const scalar_t G = (*def)[DOF_G];
//  const scalar_t H = (*def)[DOF_H];
//  const scalar_t I = (*def)[DOF_I];
//  (*def)[DOF_A] = A + u*G;
//  (*def)[DOF_B] = B + u*H;
//  (*def)[DOF_C] = C + u*I;
//  (*def)[DOF_D] = D + v*G;
//  (*def)[DOF_E] = E + v*H;
//  (*def)[DOF_F] = F + v*I;
//}


bool
Subset::is_obstructed_pixel(const scalar_t & coord_x,
  const scalar_t & coord_y) const {
  // determine which pixel the coordinates fall in:
  int_t c_x = (int_t)coord_x;
  if(coord_x - (int_t)coord_x >= 0.5) c_x++;
  int_t c_y = (int_t)coord_y;
  if(coord_y - (int_t)coord_y >= 0.5) c_y++;
  // now check if c_x and c_y are obstructed
  // Note the x and y coordinates are switched because that is how they live in the set
  // this was done for performance in loops over y then x
  std::pair<int_t,int_t> point(c_y,c_x);
  const bool obstructed = obstructed_coords_.find(point)!=obstructed_coords_.end();
  return obstructed;
}

std::set<std::pair<int_t,int_t> >
Subset::deformed_shapes(Teuchos::RCP<const std::vector<scalar_t> > deformation,
  const int_t cx,
  const int_t cy,
  const scalar_t & skin_factor){
  std::set<std::pair<int_t,int_t> > coords;
  if(!is_conformal_) return coords;
  for(size_t i=0;i<conformal_subset_def_.boundary()->size();++i){
    std::set<std::pair<int_t,int_t> > shapeCoords =
        (*conformal_subset_def_.boundary())[i]->get_owned_pixels(deformation,cx,cy,skin_factor);
    coords.insert(shapeCoords.begin(),shapeCoords.end());
  }
  return coords;
}

void
Subset::turn_off_obstructed_pixels(Teuchos::RCP<const std::vector<scalar_t> > deformation){
  assert(deformation!=Teuchos::null);

  scalar_t X=0.0,Y=0.0;
  int_t px=0,py=0;
  const bool has_blocks = !pixels_blocked_by_other_subsets_.empty();
  reset_is_deactivated_this_step();

  if(deformation->size()==DICE_DEFORMATION_SIZE){
    const scalar_t u     = (*deformation)[DICe::DOF_U];
    const scalar_t v     = (*deformation)[DICe::DOF_V];
    const scalar_t theta = (*deformation)[DICe::DOF_THETA];
    const scalar_t dudx  = (*deformation)[DICe::DOF_EX];
    const scalar_t dvdy  = (*deformation)[DICe::DOF_EY];
    const scalar_t gxy   = (*deformation)[DICe::DOF_GXY];
    scalar_t Dx=0.0,Dy=0.0;
    scalar_t dx=0.0, dy=0.0;
    scalar_t cos_t = std::cos(theta);
    scalar_t sin_t = std::sin(theta);
    for(int_t i=0;i<num_pixels_;++i){
      dx = (scalar_t)(x(i)) - cx_;
      dy = (scalar_t)(y(i)) - cy_;
      Dx = (1.0+dudx)*dx + gxy*dy;
      Dy = (1.0+dvdy)*dy + gxy*dx;
      // mapped location
      X = cos_t*Dx - sin_t*Dy + u + cx_;
      Y = sin_t*Dx + cos_t*Dy + v + cy_;

      if(is_obstructed_pixel(X,Y)){
        is_deactivated_this_step(i) = true;
      }
      else{
        is_deactivated_this_step(i) = false;
      }
      // pixels blocked by other subsets:
      if(has_blocks){
        px = ((int_t)(X + 0.5) == (int_t)(X)) ? (int_t)(X) : (int_t)(X) + 1;
        py = ((int_t)(Y + 0.5) == (int_t)(Y)) ? (int_t)(Y) : (int_t)(Y) + 1;
        if(pixels_blocked_by_other_subsets_.find(std::pair<int_t,int_t>(py,px))
            !=pixels_blocked_by_other_subsets_.end()){
          is_deactivated_this_step(i) = true;
        }
      }
    } // pixel loop
  }
  else if(deformation->size()==DICE_DEFORMATION_SIZE_AFFINE){
    TEUCHOS_TEST_FOR_EXCEPTION((*deformation)[8]==0.0,std::runtime_error,"");
    for(int_t i=0;i<num_pixels_;++i){
      // mapped location
      map_affine(x(i),y(i),X,Y,deformation);
      if(is_obstructed_pixel(X,Y)){
        is_deactivated_this_step(i) = true;
      }
      else{
        is_deactivated_this_step(i) = false;
      }
      // pixels blocked by other subsets:
      if(has_blocks){
        px = ((int_t)(X + 0.5) == (int_t)(X)) ? (int_t)(X) : (int_t)(X) + 1;
        py = ((int_t)(Y + 0.5) == (int_t)(Y)) ? (int_t)(Y) : (int_t)(Y) + 1;
        if(pixels_blocked_by_other_subsets_.find(std::pair<int_t,int_t>(py,px))
            !=pixels_blocked_by_other_subsets_.end()){
          is_deactivated_this_step(i) = true;
        }
      }
    } // pixel loop

  }
#if DICE_KOKKOS
  is_deactivated_this_step_.modify<host_space>();
  is_deactivated_this_step_.sync<device_space>();
#endif
}

void
Subset::turn_on_previously_obstructed_pixels(){
  // this assumes that the is_deactivated_this_step_ flags have already been set correctly prior
  // to calling this method.
  for(int_t px=0;px<num_pixels_;++px){
    // it's not obstructed this step, but was inactive to begin with
    if(!is_deactivated_this_step(px) && !is_active(px)){
      // take the pixel value from the deformed subset
      ref_intensities(px) = def_intensities(px);
      // set the active bit to true
      is_active(px) = true;
    }
  }
}

void
Subset::write_subset_on_image(const std::string & file_name,
  Teuchos::RCP<Image> image,
  Teuchos::RCP<const std::vector<scalar_t> > deformation){
  //create a square image that fits the extents of the subet
  const int_t w = image->width();
  const int_t h = image->height();
  const int_t ox = image->offset_x();
  const int_t oy = image->offset_y();
  intensity_t * intensities = new intensity_t[w*h];
  for(int_t m=0;m<h;++m){
    for(int_t n=0;n<w;++n){
      intensities[m*w+n] = (*image)(n,m);
    }
  }
  scalar_t mapped_x=0.0,mapped_y=0.0;
  scalar_t x_prime=0.0,y_prime=0.0;
  int_t px=0,py=0;
  if(deformation!=Teuchos::null && deformation->size()==DICE_DEFORMATION_SIZE){
    const scalar_t u = (*deformation)[DOF_U];
    const scalar_t v = (*deformation)[DOF_V];
    const scalar_t t = (*deformation)[DOF_THETA];
    const scalar_t ex = (*deformation)[DOF_EX];
    const scalar_t ey = (*deformation)[DOF_EY];
    const scalar_t g = (*deformation)[DOF_GXY];
    scalar_t dx=0.0,dy=0.0;
    scalar_t Dx=0.0,Dy=0.0;
    for(int_t i=0;i<num_pixels_;++i){
      // compute the deformed shape:
      // need to cast the x_ and y_ values since the resulting value could be negative
      dx = (scalar_t)(x(i)) - cx_;
      dy = (scalar_t)(y(i)) - cy_;
      Dx = (1.0+ex)*dx + g*dy;
      Dy = (1.0+ey)*dy + g*dx;
      // mapped location
      mapped_x = std::cos(t)*Dx - std::sin(t)*Dy + u + cx_ - ox;
      mapped_y = std::sin(t)*Dx + std::cos(t)*Dy + v + cy_ - oy;
      // get the nearest pixel location:
      px = (int_t)mapped_x;
      if(mapped_x - (int_t)mapped_x >= 0.5) px++;
      py = (int_t)mapped_y;
      if(mapped_y - (int_t)mapped_y >= 0.5) py++;
      intensities[py*w+px] = !is_active(i) ? 255
          : is_deactivated_this_step(i) ?  0
          : std::abs((def_intensities(i) - ref_intensities(i))*2);
    }
  }
  else if(deformation!=Teuchos::null && deformation->size()==DICE_DEFORMATION_SIZE_AFFINE){
    TEUCHOS_TEST_FOR_EXCEPTION((*deformation)[8]==0.0,std::runtime_error,"");
    // mapped location
    for(int_t i=0;i<num_pixels_;++i){
      // compute the deformed shape:
      map_affine(x(i),y(i),x_prime,y_prime,deformation);
      mapped_x = x_prime - ox;
      mapped_y = y_prime - oy;
      // get the nearest pixel location:
      px = (int_t)mapped_x;
      if(mapped_x - (int_t)mapped_x >= 0.5) px++;
      py = (int_t)mapped_y;
      if(mapped_y - (int_t)mapped_y >= 0.5) py++;
      intensities[py*w+px] = !is_active(i) ? 255
          : is_deactivated_this_step(i) ?  0
          : std::abs((def_intensities(i) - ref_intensities(i))*2);
    }
  }
  else{ // write the original shape of the subset
    for(int_t i=0;i<num_pixels_;++i)
      intensities[(y(i)-oy)*w+(x(i)-ox)] = 255;
  }
  utils::write_image(file_name.c_str(),w,h,intensities,true);
  delete[] intensities;
}

void
Subset::write_tiff(const std::string & file_name,
  const bool use_def_intensities){
  // determine the extents of the subset and the offsets
  int_t max_x = 0;
  int_t max_y = 0;
  int_t min_x = x(0);
  int_t min_y = y(0);
  for(int_t i=0;i<num_pixels_;++i){
    if(x(i) > max_x) max_x = x(i);
    if(x(i) < min_x) min_x = x(i);
    if(y(i) > max_y) max_y = y(i);
    if(y(i) < min_y) min_y = y(i);
  }
  //create a square image that fits the extents of the subet
  const int_t w = max_x - min_x + 1;
  const int_t h = max_y - min_y + 1;
  intensity_t * intensities = new intensity_t[w*h];
  for(int_t i=0;i<w*h;++i)
    intensities[i] = 0.0;
  for(int_t i=0;i<num_pixels_;++i){
    if(!is_active(i)){
      intensities[(y(i)-min_y)*w+(x(i)-min_x)] = 100; // color the inactive areas gray
    }
    else{
      intensities[(y(i)-min_y)*w+(x(i)-min_x)] = use_def_intensities ?
          def_intensities(i) : ref_intensities(i);
    }
  }
  utils::write_image(file_name.c_str(),w,h,intensities,true);
  delete[] intensities;
}

int_t
Subset::num_active_pixels(){
  int_t num_active = 0;
  for(int_t i=0;i<num_pixels();++i){
    if(is_active(i)&&!is_deactivated_this_step(i))
      num_active++;
  }
  return num_active;
}

scalar_t
Subset::contrast_std_dev(){
  const scalar_t mean_intensity = mean(DEF_INTENSITIES);
  scalar_t std_dev = 0.0;
  int_t num_active = 0;
  for(int_t i = 0;i<num_pixels();++i){
    if(is_active(i)&&!is_deactivated_this_step(i)){
      num_active++;
      std_dev += (def_intensities(i) - mean_intensity)*(def_intensities(i) - mean_intensity);
    }
  }
  std_dev = std::sqrt(std_dev/num_active);
  return std_dev;
}

scalar_t
Subset::noise_std_dev(Teuchos::RCP<Image> image,
  Teuchos::RCP<const std::vector<scalar_t> > deformation){

  // create the mask
  static scalar_t mask[3][3] = {{1, -2, 1},{-2,4,-2},{1,-2,1}};

  // determine the extents of the subset:
  int_t min_x = x(0);
  int_t max_x = x(0);
  int_t min_y = y(0);
  int_t max_y = y(0);
  for(int_t i=0;i<num_pixels();++i){
    if(x(i) < min_x) min_x = x(i);
    if(x(i) > max_x) max_x = x(i);
    if(y(i) < min_y) min_y = y(i);
    if(y(i) > max_y) max_y = y(i);
  }

  scalar_t u = 0.0;
  scalar_t v = 0.0;
  scalar_t x_prime = 0.0;
  scalar_t y_prime = 0.0;
  if(deformation->size()==DICE_DEFORMATION_SIZE){
    u = (*deformation)[DOF_U];
    v = (*deformation)[DOF_V];
  }else if(deformation->size()==DICE_DEFORMATION_SIZE_AFFINE){
    TEUCHOS_TEST_FOR_EXCEPTION((*deformation)[8]==0.0,std::runtime_error,"");
    map_affine(cx_,cy_,x_prime,y_prime,deformation);
    u = x_prime - cx_;
    v = y_prime - cy_;
  }else{
    TEUCHOS_TEST_FOR_EXCEPTION(true,std::runtime_error,"Error, unknown deformation vector size.");
  }
  min_x += u; max_x += u;
  min_y += v; max_y += v;

  DEBUG_MSG("Subset::noise_std_dev(): Extents of subset " << min_x << " " << max_x << " " << min_y << " " << max_y);
  const int_t h = max_y - min_y + 1;
  const int_t w = max_x - min_x + 1;
  const int_t img_h = image->height();
  const int_t img_w = image->width();
  const int_t ox = image->offset_x();
  const int_t oy = image->offset_y();
  DEBUG_MSG("Subset::noise_std_dev(): Extents of image " << ox << " " << ox + img_w << " " << oy << " " << oy + img_h);

  // ensure that the subset falls inside the image
  if(max_x >= img_w + ox || min_x < ox || max_y >= img_h + oy || min_y < oy){
    return 1.0;
  }

  scalar_t variance = 0.0;
  scalar_t conv_i = 0.0;
  // convolve and sum the intensities with the mask
  for(int_t y=min_y; y<max_y;++y){
    for(int_t x=min_x; x<max_x;++x){
      // don't convolve the edge pixels
      if(x-ox<1||x-ox>=img_w-1||y-oy<1||y-oy>=img_h-1){
        variance += std::abs((*image)(x-ox,y-oy));
      }
      else{
        conv_i = 0.0;
        for(int_t j=0;j<3;++j){
          for(int_t i=0;i<3;++i){
            conv_i += (*image)(x-ox+(i-1),y-oy+(j-1))*mask[i][j];
          }
        }
        variance += std::abs(conv_i);
      }
    }
  }
  variance *= std::sqrt(0.5*DICE_PI) / (6.0*(w-2)*(h-2));
  DEBUG_MSG("Subset::noise_std_dev(): return value " << variance);
  return variance;
}

}// End DICe Namespace
