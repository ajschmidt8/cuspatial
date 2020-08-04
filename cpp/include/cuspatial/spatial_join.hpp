/*
 * Copyright (c) 2020, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cudf/column/column_view.hpp>
#include <cudf/utilities/error.hpp>

#include <rmm/thrust_rmm_allocator.h>
#include <rmm/device_buffer.hpp>
#include <rmm/device_uvector.hpp>

#include <thrust/copy.h>
#include <thrust/device_ptr.h>
#include <thrust/execution_policy.h>
#include <thrust/fill.h>

#include <iostream>
#include <string>

#include <cudf/types.hpp>

#include <memory>

namespace cuspatial {

/**
 * @brief Search a quadtree for polygon or polyline bounding box intersections.
 *
 * @note Swaps `x_min` and `x_max` if `x_min > x_max`.
 * @note Swaps `y_min` and `y_max` if `y_min > y_max`.
 * @note `scale` is applied to (x - x_min) and (y - y_min) to convert coordinates into a Morton code
 * in 2D space.
 * @note `max_depth` should be less than 16, since Morton codes are represented as `uint32_t`.
 *
 * @param quadtree: cudf table representing a quadtree (key, level, is_quad, length, offset).
 * @param poly_bbox: cudf table of bounding boxes as four columns (x_min, y_min, x_max, y_max).
 * @param x_min The lower-left x-coordinate of the area of interest bounding box.
 * @param x_max The upper-right x-coordinate of the area of interest bounding box.
 * @param y_min The lower-left y-coordinate of the area of interest bounding box.
 * @param y_max The upper-right y-coordinate of the area of interest bounding box.
 * @param scale Scale to apply to each x and y distance from x_min and y_min.
 * @param max_depth Maximum quadtree depth at which to stop testing for intersections.
 * @param mr The optional resource to use for output device memory allocations.
 *
 * @throw cuspatial::logic_error If the quadtree table is malformed
 * @throw cuspatial::logic_error If the polygon bounding box table is malformed
 * @throw cuspatial::logic_error If scale is less than or equal to 0
 * @throw cuspatial::logic_error If x_min is greater than x_max
 * @throw cuspatial::logic_error If y_min is greater than y_max
 * @throw cuspatial::logic_error If max_depth is less than 1 or greater than 15
 *
 * @return A cudf table with two columns:
 * poly_offset - INT32 column of indices for each poly bbox that intersects with the quadtree.
 * quad_offset - INT32 column of indices for each leaf quadrant intersecting with a poly bbox.
 */
std::unique_ptr<cudf::table> quad_bbox_join(
  cudf::table_view const& quadtree,
  cudf::table_view const& poly_bbox,
  double x_min,
  double x_max,
  double y_min,
  double y_max,
  double scale,
  int8_t max_depth,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_default_resource());

/**
 * @brief Finds points in a set of (polygon, quadrant) pairs derived from spatial filtering.
 *
 * @param poly_quad_pairs Table of (quadrant, polygon) index pairs derived from spatial filtering.
 * @param quadtree Table representing a quadtree (key, level, is_quad, length, offset).
 * @param point_indices Sorted indices of quadtree points
 * @param point_x x-coordinates of points to test
 * @param point_y y-coordinates of points to test
 * @param poly_offsets Begin indices of the first ring in each polygon (i.e. prefix-sum).
 * @param ring_offsets Begin indices of the first point in each ring (i.e. prefix-sum).
 * @param poly_points_x Polygon point x-coodinates.
 * @param poly_points_y Polygon point y-coodinates.
 * @param mr The optional resource to use for output device memory allocations.
 *
 * @throw cuspatial::logic_error If the poly_quad_pairs table is malformed.
 * @throw cuspatial::logic_error If the quadtree table is malformed.
 * @throw cuspatial::logic_error If the number of point indices doesn't match the number of points.
 * @throw cuspatial::logic_error If the number of rings is less than the number of polygons.
 * @throw cuspatial::logic_error If each ring has fewer than three vertices.
 * @throw cuspatial::logic_error If the types of point and polygon vertices are different.
 *
 * @returns A cudf table of (polygon_index, point_index) pairs for each point/polyon intersection
 * pair; point_index and polygon_index are offsets into the point and polygon arrays, respectively.
 **/
std::unique_ptr<cudf::table> quadtree_point_in_polygon(
  cudf::table_view const& poly_quad_pairs,
  cudf::table_view const& quadtree,
  cudf::column_view const& point_indices,
  cudf::column_view const& point_x,
  cudf::column_view const& point_y,
  cudf::column_view const& poly_offsets,
  cudf::column_view const& ring_offsets,
  cudf::column_view const& poly_points_x,
  cudf::column_view const& poly_points_y,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_default_resource());

/**
 * @brief given a vector of paired of quadrants and polylines, for each points in a quadrant,
 * find its nearest polylines and the corresponding distance between the point and the polygon
 *
 * @param poly_quad_pairs Table of (quadrant, polygon) index pairs derived from spatial filtering.
 * @param quadtree Table representing a quadtree (key, level, is_quad, length, offset).
 * @param point_indices Sorted indices of quadtree points
 * @param point_x x-coordinates of points to test
 * @param point_y y-coordinates of points to test
 * @param poly_offsets Begin indices of the first ring in each polyline (i.e. prefix-sum)
 * @param poly_points_x Polyline point x-coordinates
 * @param poly_points_y Polyline point y-coordinates
 * @param mr The optional resource to use for output device memory allocations.
 *
 * @return a table of three columns: (point_index, polyline_index, point_to_polyline_distance)
 **/
std::unique_ptr<cudf::table> quadtree_point_to_nearest_polyline(
  cudf::table_view const& poly_quad_pairs,
  cudf::table_view const& quadtree,
  cudf::column_view const& point_indices,
  cudf::column_view const& point_x,
  cudf::column_view const& point_y,
  cudf::column_view const& poly_offsets,
  cudf::column_view const& poly_points_x,
  cudf::column_view const& poly_points_y,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_default_resource());

namespace detail {
template <typename T>
void print(std::vector<T> const& vec,
           std::ostream& os             = std::cout,
           std::string const& delimiter = ",")
{
  std::vector<double> f64s(vec.size());
  std::copy(vec.begin(), vec.end(), f64s.begin());
  os << "size: " << vec.size() << " [" << std::endl << "  ";
  std::copy(f64s.begin(), f64s.end(), std::ostream_iterator<double>(os, delimiter.data()));
  os << std::endl << "]" << std::endl;
}

template <typename T>
void print(rmm::device_vector<T> const& vec,
           std::ostream& os             = std::cout,
           std::string const& delimiter = ",",
           cudaStream_t stream          = 0)
{
  CUDA_TRY(cudaStreamSynchronize(stream));
  std::vector<T> hvec(vec.size());
  std::fill(hvec.begin(), hvec.end(), T{0});
  thrust::copy(vec.begin(), vec.end(), hvec.begin());
  print<T>(hvec, os, delimiter);
}

template <typename T>
void print(rmm::device_uvector<T> const& uvec,
           std::ostream& os             = std::cout,
           std::string const& delimiter = ",",
           cudaStream_t stream          = 0)
{
  rmm::device_vector<T> dvec(uvec.size());
  std::fill(dvec.begin(), dvec.end(), T{0});
  thrust::copy(rmm::exec_policy(stream)->on(stream), uvec.begin(), uvec.end(), dvec.begin());
  print<T>(dvec, os, delimiter, stream);
}

template <typename T>
void print(rmm::device_buffer const& buf,
           std::ostream& os             = std::cout,
           std::string const& delimiter = ",",
           cudaStream_t stream          = 0)
{
  auto ptr = thrust::device_pointer_cast<const T>(buf.data());
  rmm::device_vector<T> dvec(buf.size() / sizeof(T));
  thrust::fill(dvec.begin(), dvec.end(), T{0});
  thrust::copy(rmm::exec_policy(stream)->on(stream), ptr, ptr + dvec.size(), dvec.begin());
  print<T>(dvec, os, delimiter, stream);
}

template <typename T>
void print(cudf::column_view const& col,
           std::ostream& os             = std::cout,
           std::string const& delimiter = ",",
           cudaStream_t stream          = 0)
{
  rmm::device_vector<T> dvec(col.size());
  std::fill(dvec.begin(), dvec.end(), T{0});
  thrust::copy(rmm::exec_policy(stream)->on(stream), col.begin<T>(), col.end<T>(), dvec.begin());
  print<T>(dvec, os, delimiter, stream);
}

template <typename T>
void print(thrust::device_ptr<T> const& ptr,
           cudf::size_type size,
           std::ostream& os             = std::cout,
           std::string const& delimiter = ",",
           cudaStream_t stream          = 0)
{
  rmm::device_vector<T> dvec(size);
  std::fill(dvec.begin(), dvec.end(), T{0});
  thrust::copy(rmm::exec_policy(stream)->on(stream), ptr, ptr + size, dvec.begin());
  print<T>(dvec, os, delimiter, stream);
}

}  // namespace detail
}  // namespace cuspatial
