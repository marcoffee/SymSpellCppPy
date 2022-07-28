//
// Created by vigi99 on 27/09/20.
//

#include <filesystem>
#include <pybind11/pybind11.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include "library.h"

namespace py = pybind11;

constexpr std::streamsize DEFAULT_BUFFER_SIZE = 128 * 1024;  // 128 KiB

class py_istreambuf : public std::streambuf {
     py::object _obj, _reader;
     std::streamsize _buffer_size;
     std::unique_ptr<char_type[]> _buffer;

     protected:
     virtual int_type underflow () override {
          py::bytes read_bytes = _reader(_buffer_size);
          py::buffer_info buff = py::reinterpret_borrow<py::buffer>(read_bytes).request(false);

          if (buff.size == 0) {
               return traits_type::eof();
          }

          std::copy_n(static_cast<char*>(buff.ptr), buff.size, static_cast<char*>(_buffer.get()));
          setg(_buffer.get(), _buffer.get(), _buffer.get() + buff.size);

          return traits_type::not_eof(*egptr());
     }

     public:
     py_istreambuf (py::object obj, const std::streamsize buffer_size = DEFAULT_BUFFER_SIZE)
          : _obj(obj)
          , _reader(_obj.attr("read"))
          , _buffer_size(buffer_size)
          , _buffer(std::make_unique<char_type[]>(buffer_size))
     {
          if (_buffer_size == 0) {
               throw std::runtime_error("buffer_size should be > 0");
          }
     }
};

class py_ostreambuf : public std::streambuf {
     py::object _obj, _writer;
     std::streamsize _buffer_size;
     std::unique_ptr<char_type[]> _buffer;

     protected:
     virtual int_type sync () override {
          return overflow(traits_type::eof());
     }

     virtual int_type overflow (int_type ch) override {
          std::streamsize size = pptr() - pbase();

          if (!traits_type::eq_int_type(ch, traits_type::eof())) {
               *pptr() = traits_type::to_char_type(ch);
               ++size;
          }

          if (size > 0) {
               py::int_ const py_count = _writer(py::memoryview::from_memory(pbase(), size));

               if (py::cast<std::streamsize>(py_count) != size) {
                    return traits_type::eof();
               }

               setp(_buffer.get(), _buffer.get() + _buffer_size - 1);
          }

          return traits_type::not_eof(ch);
     }

     public:
     py_ostreambuf (py::object obj, std::streamsize const buffer_size = DEFAULT_BUFFER_SIZE)
          : _obj(obj)
          , _writer(_obj.attr("write"))
          , _buffer_size(buffer_size)
          , _buffer(std::make_unique<char_type[]>(buffer_size))
     {
          if (_buffer_size == 0) {
               throw std::runtime_error("buffer_size should be > 0");
          }

          setp(_buffer.get(), _buffer.get() + _buffer_size - 1);
     }

     virtual ~py_ostreambuf () {
          sync();
     }
};

class ptr_istreambuf : public std::streambuf {
     std::streamsize available;

     public:
     ptr_istreambuf (char_type* begin, char_type* end) {
          setg(begin, begin, end);
     }

     ptr_istreambuf (char_type* begin, std::streamsize const count)
     : ptr_istreambuf(begin, begin + count) {}
};

void check_py_buffer (py::buffer_info const& buff) {
     if (buff.strides.size() != 1) {
          throw std::domain_error("Unable to load buffer: buffer should be 1-dimensional.");
     }

     if (!PyBuffer_IsContiguous(buff.view(), 'C')) {
          throw std::domain_error("Unable to load buffer: buffer should be C-contiguous.");
     }
}

PYBIND11_MODULE(SymSpellCppPy, m) {
    m.doc() = R"pbdoc(
        Pybind11 binding for SymSpellPy
        -------------------------------
        .. currentmodule:: SymSpellCppPy
        .. autosummary::
           :toctree: _generate
           symspell
    )pbdoc";

    py::enum_<DistanceAlgorithm>(m, "DistanceAlgorithm")
            .value("LevenshteinDistance", DistanceAlgorithm::LevenshteinDistance)
            .value("DamerauOSADistance", DistanceAlgorithm::DamerauOSADistance);

    py::class_<symspellcpppy::Info>(m, "Info")
            .def(py::init<>())
            .def("set", &symspellcpppy::Info::set, "Set Info properties", py::arg("segmented_string"),
                 py::arg("corrected_string"),
                 py::arg("distance_sum"), py::arg("log_prob_sum"))
            .def("get_segmented", &symspellcpppy::Info::getSegmented, "The word segmented string.")
            .def("get_corrected", &symspellcpppy::Info::getCorrected,
                 "The word segmented and spelling corrected string.")
            .def("get_distance", &symspellcpppy::Info::getDistance,
                 "The Edit distance sum between input string and corrected string.")
            .def("get_probability", &symspellcpppy::Info::getProbability,
                 "The Sum of word occurrence probabilities in log scale (a measure of how common and probable the corrected segmentation is).")
            .def_property_readonly("segmented_string", &symspellcpppy::Info::getSegmented, "The word segmented string.")
            .def_property_readonly("corrected_string", &symspellcpppy::Info::getCorrected,
                                   "The word segmented and spelling corrected string.")
            .def_property_readonly("distance_sum", &symspellcpppy::Info::getDistance,
                                   "The Edit distance sum between input string and corrected string.")
            .def_property_readonly("log_prob_sum", &symspellcpppy::Info::getProbability,
                                   "The Sum of word occurrence probabilities in log scale (a measure of how common and probable the corrected segmentation is).")
            .def("__repr__",
                 [](const symspellcpppy::Info &a) {
                     return "<Info corrected string ='" + a.getCorrected() + "'>";
                 }
            );

    py::class_<SuggestItem>(m, "SuggestItem")
            .def(py::init<xstring, int, int64_t>())
            .def(py::self == py::self, "Compare ==")
            .def(py::self < py::self, "Order by distance ascending, then by frequency count descending.")
            .def("__repr__",
                 [](const SuggestItem &a) {
                     return a.term + ", " + std::to_string(a.distance) + ", " + std::to_string(a.count);
                 }
            )
            .def("__str__",
                 [](const SuggestItem &a) {
                     return a.term + ", " + std::to_string(a.distance) + ", " + std::to_string(a.count);
                 }
            )
            .def_readwrite("term", &SuggestItem::term, "The suggested correctly spelled word.")
            .def_readwrite("distance", &SuggestItem::distance,
                           "Edit distance between searched for word and suggestion.")
            .def_readwrite("count", &SuggestItem::count,
                           "Frequency of suggestion in the dictionary (a measure of how common the word is).");

    py::enum_<symspellcpppy::Verbosity>(m, "Verbosity")
            .value("TOP", symspellcpppy::Verbosity::Top,
                   "The suggestion with the highest term frequency of the suggestions of smallest edit distance found.")
            .value("CLOSEST", symspellcpppy::Verbosity::Closest,
                   "All suggestions of smallest edit distance found, the suggestions are ordered by term frequency.")
            .value("ALL", symspellcpppy::Verbosity::All,
                   "All suggestions <= maxEditDistance, the suggestions are ordered by edit distance, then by term frequency (slower, no early termination).")
            .export_values();

    py::class_<symspellcpppy::SymSpell>(m, "SymSpell")
            .def(py::init<int, int, int, int, unsigned char, DistanceAlgorithm>(), "SymSpell builder options",
                 py::arg("max_dictionary_edit_distance") = DEFAULT_MAX_EDIT_DISTANCE,
                 py::arg("prefix_length") = DEFAULT_PREFIX_LENGTH,
                 py::arg("count_threshold") = DEFAULT_COUNT_THRESHOLD,
                 py::arg("initial_capacity") = DEFAULT_INITIAL_CAPACITY,
                 py::arg("compact_level") = DEFAULT_COMPACT_LEVEL,
                 py::arg("distance_algorithm") = DEFAULT_DISTANCE_ALGORITHM
            )
            .def(py::self == py::self, "Compare ==")
            .def(py::self != py::self, "Compare !=")
            .def("__repr__", [](const symspellcpppy::SymSpell &sym) {
                    std::stringstream out;
                    out << sym;
                    return py::str(std::move(out).str());
                 })
            .def("__str__", [](const symspellcpppy::SymSpell &sym) {
                    std::stringstream out;
                    out << sym;
                    return py::str(std::move(out).str());
                 })
            .def("word_count", &symspellcpppy::SymSpell::WordCount, "Number of words entered.")
            .def("max_length", &symspellcpppy::SymSpell::MaxLength, "Max length of words entered.")
            .def("entry_count", &symspellcpppy::SymSpell::EntryCount, "Total number of deletes formed.")
            .def("count_threshold", &symspellcpppy::SymSpell::CountThreshold,
                 "Frequency of word so that its considered a valid word for spelling correction.")
            .def("distance_algorithm", &symspellcpppy::SymSpell::GetDistanceAlgorithm, "Distance algorithm used.")
            .def("create_dictionary_entry", py::overload_cast<const std::string_view &, int64_t>(
                    &symspellcpppy::SymSpell::CreateDictionaryEntry),
                 "Create/Update an entry in the dictionary.", py::arg("key"), py::arg("count"))
            .def("delete_dictionary_entry", &symspellcpppy::SymSpell::DeleteDictionaryEntry,
                 "Delete the key from the dictionary & updates internal representation accordingly.",
                 py::arg("key"))
            .def("load_bigram_dictionary", py::overload_cast<const std::string &, int, int, xchar>(
                    &symspellcpppy::SymSpell::LoadBigramDictionary),
                 "Load multiple dictionary entries from a file of word/frequency count pairs.",
                 py::arg("corpus"),
                 py::arg("term_index"),
                 py::arg("count_index"),
                 py::arg("separator") = DEFAULT_SEPARATOR_CHAR)
            .def("load_dictionary", py::overload_cast<const std::string &, int, int, xchar>(
                    &symspellcpppy::SymSpell::LoadDictionary),
                 "Load multiple dictionary entries from a file of word/frequency count pairs.",
                 py::arg("corpus"),
                 py::arg("term_index"),
                 py::arg("count_index"),
                 py::arg("separator") = DEFAULT_SEPARATOR_CHAR)
            .def("create_dictionary", py::overload_cast<const std::string &>(
                    &symspellcpppy::SymSpell::CreateDictionary),
                 "Load multiple dictionary words from a file containing plain text.",
                 py::arg("corpus"))
            .def("purge_below_threshold_words", &symspellcpppy::SymSpell::PurgeBelowThresholdWords,
                 "Remove all below threshold words from the dictionary.")
            .def("lookup", py::overload_cast<const xstring_view &, symspellcpppy::Verbosity>(
                    &symspellcpppy::SymSpell::Lookup, py::const_),
                 " Find suggested spellings for a given input word, using the maximum\n"
                 "    edit distance specified during construction of the SymSpell dictionary.",
                 py::arg("input"),
                 py::arg("verbosity"))
            .def("lookup", py::overload_cast<const xstring_view &, symspellcpppy::Verbosity, int>(
                    &symspellcpppy::SymSpell::Lookup, py::const_),
                 " Find suggested spellings for a given input word, using the maximum\n"
                 "    edit distance provided to the function.",
                 py::arg("input"),
                 py::arg("verbosity"),
                 py::arg("max_edit_distance"))
            .def("lookup", py::overload_cast<const xstring_view &, symspellcpppy::Verbosity, int, bool>(
                    &symspellcpppy::SymSpell::Lookup, py::const_),
                 " Find suggested spellings for a given input word, using the maximum\n"
                 "    edit distance provided to the function and include input word in suggestions, if no words within edit distance found.",
                 py::arg("input"),
                 py::arg("verbosity"),
                 py::arg("max_edit_distance"),
                 py::arg("include_unknown"))
            .def("lookup", py::overload_cast<const xstring_view &, symspellcpppy::Verbosity, int, bool, bool>(
                    &symspellcpppy::SymSpell::Lookup, py::const_),
                 " Find suggested spellings for a given input word, using the maximum\n"
                 "    edit distance provided to the function and include input word in suggestions, if no words within edit distance found & preserve transfer casing.",
                 py::arg("input"),
                 py::arg("verbosity"),
                 py::arg("max_edit_distance") = DEFAULT_MAX_EDIT_DISTANCE,
                 py::arg("include_unknown") = false,
                 py::arg("transfer_casing") = false)
            .def("lookup_compound", py::overload_cast<const xstring &>(
                    &symspellcpppy::SymSpell::LookupCompound, py::const_),
                 " LookupCompound supports compound aware automatic spelling correction of multi-word input strings with three cases:\n"
                 "    1. mistakenly inserted space into a correct word led to two incorrect terms \n"
                 "    2. mistakenly omitted space between two correct words led to one incorrect combined term\n"
                 "    3. multiple independent input terms with/without spelling errors",
                 py::arg("input"))
            .def("lookup_compound", py::overload_cast<const xstring &, int>(
                    &symspellcpppy::SymSpell::LookupCompound, py::const_),
                 " LookupCompound supports compound aware automatic spelling correction of multi-word input strings with three cases:\n"
                 "    1. mistakenly inserted space into a correct word led to two incorrect terms \n"
                 "    2. mistakenly omitted space between two correct words led to one incorrect combined term\n"
                 "    3. multiple independent input terms with/without spelling errors",
                 py::arg("input"),
                 py::arg("max_edit_distance"))
            .def("lookup_compound", py::overload_cast<const xstring &, int, bool>(
                    &symspellcpppy::SymSpell::LookupCompound, py::const_),
                 " LookupCompound supports compound aware automatic spelling correction of multi-word input strings with three cases:\n"
                 "    1. mistakenly inserted space into a correct word led to two incorrect terms \n"
                 "    2. mistakenly omitted space between two correct words led to one incorrect combined term\n"
                 "    3. multiple independent input terms with/without spelling errors",
                 py::arg("input"),
                 py::arg("max_edit_distance"),
                 py::arg("transfer_casing"))
            .def("word_segmentation", py::overload_cast<const xstring &>(
                    &symspellcpppy::SymSpell::WordSegmentation, py::const_),
                 " WordSegmentation divides a string into words by inserting missing spaces at the appropriate positions\n"
                 "    misspelled words are corrected and do not affect segmentation\n"
                 "    existing spaces are allowed and considered for optimum segmentation",
                 py::arg("input"))
            .def("word_segmentation", py::overload_cast<const xstring &, int>(
                    &symspellcpppy::SymSpell::WordSegmentation, py::const_),
                 " WordSegmentation divides a string into words by inserting missing spaces at the appropriate positions\n"
                 "    misspelled words are corrected and do not affect segmentation\n"
                 "    existing spaces are allowed and considered for optimum segmentation",
                 py::arg("input"),
                 py::arg("max_edit_distance"))
            .def("word_segmentation", py::overload_cast<const xstring &, int, int>(
                    &symspellcpppy::SymSpell::WordSegmentation, py::const_),
                 " WordSegmentation divides a string into words by inserting missing spaces at the appropriate positions\n"
                 "    misspelled words are corrected and do not affect segmentation\n"
                 "    existing spaces are allowed and considered for optimum segmentation",
                 py::arg("input"),
                 py::arg("max_edit_distance"),
                 py::arg("max_segmentation_word_length"))
            .def("save_pickle", [](const symspellcpppy::SymSpell &sym, const std::string &filepath) {
                     std::ofstream binary_path(filepath, std::ios::out | std::ios::app | std::ios::binary);
                     if (binary_path.is_open()) {
                         cereal::BinaryOutputArchive ar(binary_path);
                         ar(sym);
                     } else {
                         throw std::invalid_argument("Cannot save to file: " + filepath);
                     }
                 }, "Legacy save internal representation to file",
                 py::arg("filepath"))
            .def("load_pickle", [](symspellcpppy::SymSpell &sym, const std::string &filepath) {
                     if (Helpers::file_exists(filepath)) {
                         std::ifstream binary_path(filepath, std::ios::binary);
                         cereal::BinaryInputArchive ar(binary_path);
                         ar(sym);
                     } else {
                         throw std::invalid_argument("Unable to load file from filepath: " + filepath);
                     }
                 }, "Legacy load internal representation from file",
                 py::arg("filepath"))
            .def("save_pickle_bytes", [](const symspellcpppy::SymSpell &sym) {
                    std::ostringstream binary_stream(std::ios::out | std::ios::binary);
                    cereal::BinaryOutputArchive ar(binary_stream);
                    ar(sym);

                    return py::bytes(binary_stream.str());
                 }, "Save internal representation to bytes")
            .def("load_pickle_bytes", [](symspellcpppy::SymSpell &sym, py::buffer bytes) {
                    py::buffer_info buff = bytes.request();
                    check_py_buffer(buff);

                    ptr_istreambuf cpp_buff(reinterpret_cast<char*>(buff.ptr), buff.size * buff.itemsize);
                    std::istream cpp_stream(&cpp_buff);

                    cereal::BinaryInputArchive ar(cpp_stream);
                    ar(sym);
                 }, "Load internal representation from buffers, such as 'bytes' and 'memoryview'",
                 py::arg("bytes"))
            .def("to_file", [](
               const symspellcpppy::SymSpell &sym,
               const std::variant<std::string, std::filesystem::path> &filepath) {
                     std::ofstream binary_path = std::visit([] (auto&& path) -> std::ofstream {
                          return std::ofstream(path, std::ios::out | std::ios::app | std::ios::binary);
                     }, filepath);

                     if (binary_path.is_open()) {
                         sym.to_stream(binary_path);

                     } else {
                         throw std::invalid_argument(std::visit([] (auto&& path) -> std::string {
                              return "Cannot save to file: " + std::string(path);
                         }, filepath));
                     }
                 }, "Save internal representation to file",
                 py::arg("filepath"))
            .def_static("from_file", [](const std::variant<std::string, std::filesystem::path> &filepath) {
                    std::ifstream binary_path = std::visit([] (auto&& path) -> std::ifstream {
                         return std::ifstream(path, std::ios::binary);
                    }, filepath);

                    if (binary_path.is_open()) {
                         return symspellcpppy::SymSpell::from_stream(binary_path);

                    } else {
                         throw std::invalid_argument(std::visit([] (auto&& path) -> std::string {
                              return "Unable to load file from filepath: " + std::string(path);
                         }, filepath));
                    }
                 }, "Load internal representation from file",
                 py::arg("filepath"))
            .def("to_bytes", [](const symspellcpppy::SymSpell &sym) {
                    std::ostringstream sstr;
                    sym.to_stream(sstr);
                    return py::bytes(std::move(sstr).str());
                 }, "Save internal representation to bytes")
            .def_static("from_bytes", [](py::buffer bytes) {
                    py::buffer_info buff = bytes.request();
                    check_py_buffer(buff);

                    ptr_istreambuf cpp_buff(reinterpret_cast<char*>(buff.ptr), buff.size * buff.itemsize);
                    std::istream cpp_stream(&cpp_buff);
                    return symspellcpppy::SymSpell::from_stream(cpp_stream);
                 }, "Load internal representation from buffers, such as 'bytes' and 'memoryview'",
                 py::arg("bytes"))
            .def("to_stream", [](const symspellcpppy::SymSpell &sym, py::object py_stream, std::streamsize const buffer_size) {
                    py_ostreambuf cpp_buff(py_stream, buffer_size);
                    std::ostream cpp_stream(&cpp_buff);
                    sym.to_stream(cpp_stream);
                 }, "Save internal representation to python stream",
                 py::arg("stream"), py::arg("buffer_size") = DEFAULT_BUFFER_SIZE)
            .def_static("from_stream", [](py::object py_stream, std::streamsize const buffer_size) {
                    py_istreambuf cpp_buff(py_stream, buffer_size);
                    std::istream cpp_stream(&cpp_buff);
                    return symspellcpppy::SymSpell::from_stream(cpp_stream);
                 }, "Load internal representation from python stream",
                 py::arg("stream"), py::arg("buffer_size") = DEFAULT_BUFFER_SIZE)
            ;

    m.attr("DEFAULT_SEPARATOR_CHAR") = DEFAULT_SEPARATOR_CHAR;
    m.attr("DEFAULT_MAX_EDIT_DISTANCE") = DEFAULT_MAX_EDIT_DISTANCE;
    m.attr("DEFAULT_PREFIX_LENGTH") = DEFAULT_PREFIX_LENGTH;
    m.attr("DEFAULT_COUNT_THRESHOLD") = DEFAULT_COUNT_THRESHOLD;
    m.attr("DEFAULT_INITIAL_CAPACITY") = DEFAULT_INITIAL_CAPACITY;
    m.attr("DEFAULT_COMPACT_LEVEL") = DEFAULT_COMPACT_LEVEL;
    m.attr("DEFAULT_DISTANCE_ALGORITHM") = DEFAULT_DISTANCE_ALGORITHM;
    m.attr("DEFAULT_BUFFER_SIZE") = DEFAULT_BUFFER_SIZE;
}
