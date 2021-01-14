/**
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 **/

#ifndef DGTAL_IMAGECONTAINERBYSTLVECTOR_DECLARE_PY_H
#define DGTAL_IMAGECONTAINERBYSTLVECTOR_DECLARE_PY_H

#include "dgtal_pybind11_common.h"

#include "DGtal/images/ImageContainerBySTLVector.h"
#include "ImageContainerBySTLVector_types_py.h"

// py::type::of<int> fails at compilation time.
// But it works for any wrapped type.
// The following is a SFINAE workaround to return also a type for
// pure python types (not wrapped by pybind11).
// This implementation found a bug in pybind11: https://github.com/pybind/pybind11/issues/2700
template<typename T>
using IsCPPType = typename std::enable_if<std::is_base_of<pybind11::detail::type_caster_generic, pybind11::detail::make_caster<T>>::value>::type;

template<typename T>
using IsNotCPPType = typename std::enable_if<!std::is_base_of<pybind11::detail::type_caster_generic, pybind11::detail::make_caster<T>>::value>::type;

template<typename TT>
void def_TValue(
        pybind11::class_<TT> & py_class,
        IsCPPType<typename TT::Value> * = nullptr) {
    py_class.def_property_readonly_static("TValue",
            [](pybind11::object /* self */ ) {
            return pybind11::type::of<typename TT::Value>();
            });
}
template<typename TT>
void def_TValue(pybind11::class_<TT> & py_class,
        IsNotCPPType<typename TT::Value> * = nullptr) {
    py_class.def_property_readonly_static("TValue",
            [](pybind11::object /* self */ ) {
            return pybind11::type::of(
                        pybind11::cast(typename TT::Value())
                    );
            });
}

template<typename TT>
void def_buffer_bridge(
        pybind11::class_<TT> & py_class) {
    namespace py = pybind11;
    using TTPoint = typename TT::Point;
    using TTValue = typename TT::Value;
    using TTDomain = typename TT::Domain;
    // ----------------------- Bridges ----------------------------------------
    // Python buffers (requires py::buffer_protocol in py_class instantiation)
    /* Implements interface with the buffer protocol.
     * For example: `numpy.array(an_instance, copy=False
     * Note that the order of the linear array on the image container is F_contiguous
     * (column major).
     */
    py_class.def_buffer([](TT &self) -> py::buffer_info {
        const auto dextent = self.extent();
        // Note that shape would be the reverse of dextent for c_contiguous (row-major)
        // But the container works in f_contiguous (column major).
        const std::vector<ssize_t> shape(dextent.begin(), dextent.end());
        const auto valuesize = static_cast<ssize_t>(sizeof(TTValue));
        // DGtal::Linearizer is column major by default
        return py::buffer_info(
            self.data(),                                  /* Pointer to buffer */
            valuesize,                                    /* Size of one scalar */
            py::format_descriptor<TTValue>::format(),     /* Python struct-style format descriptor */
            TTPoint::dimension,                           /* Number of dimensions */
            shape,                                        /* Shape, buffer dimensions */
            // c_strides == row major. --->, --->
            // f_strides == col major. |, |
            pybind11::detail::f_strides(shape, valuesize) /* Strides (in bytes) for each index */
            );
        });

    // Allows: ImageContainer(np_array, lower_bound=Point(0,0,0), order='F')
    py_class.def(py::init([](py::buffer buf,
                    const TTPoint & lower_bound,
                    const std::string order) {
        /* Note(phcerdan): Adapted from numpy/stl_bind.h vector_buffer */
        /* Request a buffer descriptor from Python */
        auto info = buf.request();
        const auto valuesize = static_cast<ssize_t>(sizeof(TTValue));

        /* Sanity checks */
        if (info.ndim != TTPoint::dimension) {
            throw py::type_error("The dimension of the input buffer (" +
                    std::to_string(info.ndim) +
                    ") is invalid for the dimension of this type (" +
                    std::to_string(TTPoint::dimension) + ").");
        }
        if(valuesize != info.itemsize) {
            throw py::type_error("Data types have different size. Python: " +
                    std::to_string(info.itemsize) +
                    ", C++: " + std::to_string(valuesize) + ".");
        }
        if (info.strides[0] % valuesize ) {
            throw py::type_error("The strides of the input buffer (" +
                    std::to_string(info.strides[0]) +
                    ") are not multiple of the expected value size (" +
                    std::to_string(valuesize) + "). Maybe the data types or the order (F or C) are incompatible for this conversion.");
        }
        if (!py::detail::compare_buffer_info<TTValue>::compare(info)) {
            throw py::type_error("Format mismatch (Python: " + info.format + " C++: " + py::format_descriptor<TTValue>::format() + ")");
        }

        TTPoint upper_bound = lower_bound;
        // Adapted shape to deal with f_contiguous and c_contiguous
        // f_contiguous use begin, c_contiguous is reversed (rbegin, rend)
        const auto adapted_shape = (order == "F") ?
            decltype(info.shape)(info.shape.begin(), info.shape.end()) :
            decltype(info.shape)(info.shape.rbegin(), info.shape.rend());

        for(size_t i = 0; i < TTPoint::dimension; ++i) {
            // In DGtal the upper_bound is included in a domain.
            // Bounds are indices, not sizes.
            // Shape is a size, we adapt it to index with -1
            upper_bound[i] += adapted_shape[i] - 1;
        }
        // Construct with domain
        TTDomain domain(lower_bound, upper_bound);
        auto out = TT(domain);
        // Populate data of the container, copy is needed, vector has to own its memory.
        memcpy(out.data(), static_cast<TTValue*>(info.ptr), info.size * valuesize);
        return out;
        }),
R"(Construct ImageContainer from an appropiate python buffer and a lower bound.
Parameters
----------
buffer: python buffer
    The dimensions of the buffer must match those in this ImageContainer type.
    The buffer must be F_contiguous (column major).
lower_bound: Point (Optional)
    Defaults to TPoint.zero of this ImageContainer.
order: F or C (Optional)
    Order of the input buffer.
    Defaults to F, i.e F_contiguous (column major)
    C is C_contiguous (row major).
)", py::arg("buffer"), py::arg("lower_bound") = TTPoint::zero, py::arg("order") = "F");
}

template<typename TImageContainerBySTLVector>
pybind11::class_<TImageContainerBySTLVector> declare_ImageContainerBySTLVector_with_buffer_protocol(pybind11::module &m,
    const std::string &typestr) {
    namespace py = pybind11;
    using TT = TImageContainerBySTLVector;
    return py::class_<TT>(m, typestr.c_str(), py::buffer_protocol());
}
template<typename TImageContainerBySTLVector>
pybind11::class_<TImageContainerBySTLVector> declare_ImageContainerBySTLVector(pybind11::module &m,
    const std::string &typestr) {
    namespace py = pybind11;
    using TT = TImageContainerBySTLVector;
    return py::class_<TT>(m, typestr.c_str());
}

template<typename TImageContainerBySTLVector>
void def_common_functions(pybind11::class_<TImageContainerBySTLVector> & py_class, const std::string &typestr) {
    namespace py = pybind11;
    using TT = TImageContainerBySTLVector;
    using TTPoint = typename TT::Point;
    using TTValue = typename TT::Value;
    using TTDomain = typename TT::Domain;
    using Vector = TT;
    using T = typename Vector::value_type;
    using DiffType = typename Vector::difference_type;
    using SizeType = typename Vector::size_type;
    using Class_ = py::class_<TT>;
    auto & cl = py_class;

    auto wrap_i = [](DiffType i, SizeType n) {
        if (i < 0)
            i += n;
        if (i < 0 || (SizeType)i >= n)
            throw py::index_error();
        return i;
    };

    // ----------- Start std::vector binding ------------/

    // Declare the buffer interface if a buffer_protocol() is passed in
    py::detail::vector_buffer<TT, Class_>(cl);

    // Register copy constructor (if possible)
    py::detail::vector_if_copy_constructible<Vector, Class_>(cl);

    // Register stream insertion operator (if possible)
    py::detail::vector_if_insertion_operator<Vector, Class_>(cl, typestr);

    // Modifiers require copyable vector value type and default constructor.
    // Also it adds append, pop, etc, that we are not really interested.
    // py::detail::vector_modifiers<Vector, Class_>(cl);
    cl.def("__setitem__",
        [wrap_i](Vector &v, DiffType i, const T &t) {
            i = wrap_i(i, v.size());
            v[(SizeType)i] = t;
        }
    );

    cl.def("__delitem__",
        [wrap_i](Vector &v, DiffType i) {
            i = wrap_i(i, v.size());
            v.erase(v.begin() + i);
        },
        "Delete the list elements at index ``i``"
    );

    // Accessor and iterator; return by value if copyable, otherwise we return by ref + keep-alive
    py::detail::vector_accessor<Vector, Class_>(cl);

    cl.def("__bool__",
        [](const Vector &v) -> bool {
            return !v.empty();
        },
        "Check whether the list is nonempty"
    );

    cl.def("__len__", &Vector::size);

    // ----------- End std::vector binding ------------/

    // ----------------------- Constructors -----------------------------------
    py_class.def(py::init<const TTDomain &>());

    // ----------------------- Python operators -------------------------------
    // Accessors with points
    py_class.def("__getitem__",
            [](const TT &self, const TTPoint &point) {
            return self.operator()(point);
            });
    py_class.def("__setitem__",
            [](TT &self, const TTPoint &point, const TTValue & value) {
            self.setValue(point, value);
            });
    // ----------------------- Class operators --------------------------------

    // ----------------------- Class functions --------------------------------
    py_class.def("translateDomain", &TT::translateDomain,
R"(Translate the underlying domain by a [shift]."
Parameters
----------
shift: Point
    Any point to shift the domain
)", py::arg("shift"));

    py_class.def("setValue",
            py::detail::overload_cast_impl<const TTPoint&, const TTValue&>()(&TT::setValue),
R"(Set a value on an Image at a position specified by [point].
Pre: The point must be in the image domain.

Note, you can also use python accesors with points to get/set the points.

Parameters
----------
point: Point
    Any point inside the domain.
value: ImageContainer::Value
    Any value of the corresponding type of the container.
)", py::arg("point"), py::arg("value"));

    py_class.def("getValue",
            [](const TT& self, const TTPoint & point) {
            self.operator()(point);
            },
R"(Get a value on an Image at a position specified by [point].
Pre: The point must be in the image domain.

Note, you can also use python accesors with points to get/set the points.

Parameters
----------
point: Point
    Any point inside the domain.
)", py::arg("point"));

    // --- Slicing with subdomains ----

    // Note(phcerdan): It would be nice to have slicing with subdomains:
    // img[subdomain] and/or img[p_lower:p_upper]
    // Use DGtal::Linearizer<Domain> for maps:
    //   domain_point->linear_index, linear_index->domain_point

    // ----------------------- Class data -------------------------------------

    py_class.def_property_readonly("domain", &TT::domain,
            "Returns the domain of the container.");
    py_class.def_property_readonly("extent", &TT::extent,
            "Returns the extent of the domain.");

    py_class.def_property_readonly_static("TPoint",
            [](py::object /* self */) {
            return py::type::of<TTPoint>();
            });
    py_class.def_property_readonly_static("TDomain",
            [](py::object /* self */) {
            return py::type::of<TTDomain>();
            });
    def_TValue<TT>(py_class);

    // ----------------------- Print / Display --------------------------------
    py_class.def("__str__", [](const TT & self) {
        std::stringstream os;
        self.selfDisplay(os);
        return os.str();
    });

    py_class.def("__repr__", [typestr](const TT & self) {
        std::stringstream os;
        os << typestr;
        os << ": ";
        self.selfDisplay(os);
        return os.str();
        });
}
#endif
