"""
    To Check install
    pip install pytest pytest-benchmark symspellpy SymSpellCppPy
    pytest benchmark.py
"""

from symspellpy import SymSpell as SymSpellPy, Verbosity as VerbosityPy
from SymSpellCppPy import SymSpell as SymSpellCpp, Verbosity as VerbosityCpp
import pytest
import os

dict_path = "resources/frequency_dictionary_en_82_765.txt"
text_dict_path = "tests/fortests/big_modified.txt"

@pytest.mark.benchmark(
    group="create_dict",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_create_dict_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    benchmark(sym_spell.create_dictionary, text_dict_path)


@pytest.mark.benchmark(
    group="create_dict",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_create_dict_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    benchmark(sym_spell.create_dictionary, text_dict_path)


@pytest.mark.benchmark(
    group="load_dict",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_load_dict_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    benchmark(sym_spell.load_dictionary, dict_path, term_index=0, count_index=1, separator=" ")


@pytest.mark.benchmark(
    group="load_dict",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_load_dict_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    benchmark(sym_spell.load_dictionary, dict_path, term_index=0, count_index=1, separator=" ")


@pytest.mark.benchmark(
    group="lookup",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_lookup_term_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    input_term = "mEmEbers"
    result = benchmark(sym_spell.lookup, input_term, VerbosityPy.CLOSEST, max_edit_distance=2, transfer_casing=True)
    assert (result[0].term.lower() == "members")


@pytest.mark.benchmark(
    group="lookup",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_lookup_term_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    input_term = "mEmEbers"
    result = benchmark(sym_spell.lookup, input_term, VerbosityCpp.CLOSEST, max_edit_distance=2)
    assert (result[0].term == "members")


@pytest.mark.benchmark(
    group="lookup_compound",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_lookup_compound_term_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    input_term = "whereis th elove"
    result = benchmark(sym_spell.lookup_compound, input_term, max_edit_distance=2)
    assert (result[0].term == "whereas the love")


@pytest.mark.benchmark(
    group="lookup_compound",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_lookup_compound_term_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    input_term = "whereis th elove"
    result = benchmark(sym_spell.lookup_compound, input_term, max_edit_distance=2)
    assert (result[0].term == "whereas the love")


@pytest.mark.benchmark(
    group="word_segmentation",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_word_segmentation_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    input_term = "thequickbrownfoxjumpsoverthelazydog"
    result = benchmark(sym_spell.word_segmentation, input_term, max_edit_distance=0, max_segmentation_word_length=5)
    assert (result.segmented_string == "the quick brown fox jumps over the lazy dog")


@pytest.mark.benchmark(
    group="word_segmentation",
    min_rounds=5,
    disable_gc=True,
    warmup=False
)
def test_word_segmentation_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    input_term = "thequickbrownfoxjumpsoverthelazydog"
    result = benchmark(sym_spell.word_segmentation, input_term, max_edit_distance=0, max_segmentation_word_length=5)
    assert (result.segmented_string == "the quick brown fox jumps over the lazy dog")

@pytest.mark.benchmark(
    group="save_pickle",
    min_rounds=1,
    disable_gc=True,
    warmup=False
)
def test_save_pickle_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    os.makedirs("temp_py", exist_ok=True)
    result = benchmark(sym_spell.save_pickle, "temp_py/temp.pk")
    assert (sym_spell._max_length == 28)

@pytest.mark.benchmark(
    group="save_pickle",
    min_rounds=1,
    disable_gc=True,
    warmup=False
)
def test_save_pickle_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, term_index=0, count_index=1, separator=" ")
    os.makedirs("temp_cpppy", exist_ok=True)
    result = benchmark(sym_spell.save_pickle, "temp_cpppy/temp.bin")
    assert (sym_spell.max_length() == 28)

@pytest.mark.benchmark(
    group="load_pickle",
    min_rounds=1,
    disable_gc=True,
    warmup=False
)
def test_load_pickle_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    benchmark(sym_spell.load_pickle, "temp_py/temp.pk")
    os.remove("temp_py/temp.pk")
    os.rmdir("temp_py")
    assert (sym_spell.lookup("tke", VerbosityPy.CLOSEST)[0].term == "the")

@pytest.mark.benchmark(
    group="load_pickle",
    min_rounds=1,
    disable_gc=True,
    warmup=False
)
def test_load_pickle_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    benchmark(sym_spell.load_pickle, "temp_cpppy/temp.bin")
    os.remove("temp_cpppy/temp.bin")
    os.rmdir("temp_cpppy")
    assert (sym_spell.lookup("tke", VerbosityCpp.CLOSEST)[0].term == "the")

@pytest.mark.benchmark(
    group="lookup_transfer_casing",
    min_rounds=100,
    disable_gc=True,
    warmup=False
)
def test_lookup_transfer_casing_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.create_dictionary_entry("steam", 4)
    result = benchmark(sym_spell.lookup, "StreaM", VerbosityPy.TOP, 2,
                              transfer_casing=True)
    assert (result[0].term == "SteaM")

@pytest.mark.benchmark(
    group="lookup_transfer_casing",
    min_rounds=100,
    disable_gc=True,
    warmup=False
)
def test_lookup_transfer_casing_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.create_dictionary_entry("steam", 4)
    result = benchmark(sym_spell.lookup, "StreaM", VerbosityCpp.TOP, 2,
                              transfer_casing=True)
    assert (result[0].term == "SteaM")

@pytest.mark.benchmark(
    group="lookup_compund_transfer_casing",
    min_rounds=100,
    disable_gc=True,
    warmup=False
)
def test_lookup_compund_transfer_casing_symspellpy(benchmark):
    sym_spell = SymSpellPy(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, 0, 1)
    typo = ("Whereis th elove hehaD Dated forImuch of thepast who "
            "couqdn'tread in sixthgrade AND ins pired him")
    correction = ("Whereas the love heaD Dated for much of the past "
                  "who couldn't read in sixth grade AND inspired him")
    results = benchmark(sym_spell.lookup_compound, typo, 2, transfer_casing=True)
    assert (results[0].term == correction)

@pytest.mark.benchmark(
    group="lookup_compund_transfer_casing",
    min_rounds=100,
    disable_gc=True,
    warmup=False
)
def test_lookup_compund_transfer_casing_symspellcpppy(benchmark):
    sym_spell = SymSpellCpp(max_dictionary_edit_distance=2, prefix_length=7)
    sym_spell.load_dictionary(dict_path, 0, 1)
    typo = ("Whereis th elove hehaD Dated forImuch of thepast who "
            "couqdn'tread in sixthgrade AND ins pired him")
    correction = ("Whereas the love heaD Dated for much of the past "
                  "who couldn't read in sixth grade AND inspired him")
    results = benchmark(sym_spell.lookup_compound, typo, 2, transfer_casing=True)
    assert (results[0].term == correction)
