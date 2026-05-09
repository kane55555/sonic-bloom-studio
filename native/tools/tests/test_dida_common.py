"""pytest tests for native/tools/dida_common.py
Run from repo root:  pytest native/tools/tests/
"""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from dida_common import (
    classify_category, detect_note, detect_velocity,
    default_root_for, category_template, NEW_CATEGORIES,
)


class TestCategories:
    def test_eleven_categories(self):
        assert len(NEW_CATEGORIES) == 11
        assert "DrillBells" in NEW_CATEGORIES
        assert "Bass808" in NEW_CATEGORIES
        assert "Uncategorized" in NEW_CATEGORIES

    def test_keyword_classification(self):
        assert classify_category("dark_bell_C5")[0] == "DrillBells"
        assert classify_category("heavy_808_sub")[0] == "Bass808"
        assert classify_category("sad_piano")[0] == "PainPianos"
        assert classify_category("alien_lead_mono")[0] == "AlienLeads"
        assert classify_category("big_riser")[0] == "FXRisers"

    def test_legacy_folder_fallback(self):
        cat, conf, src = classify_category("noteword", parent_dir="Glockenspiel")
        assert cat == "DrillBells"
        assert "legacy" in src

    def test_unknown_falls_back(self):
        cat, conf, _ = classify_category("xyzzy_random")
        assert cat == "Uncategorized"
        assert conf < 0.5


class TestNoteDetection:
    def test_detects_note_with_octave(self):
        result = detect_note("Dark_Bell_C5")
        assert result is not None
        assert result == ("C5", 72)

    def test_detects_sharp(self):
        assert detect_note("Pad_F#3")[1] == 54

    def test_detects_flat(self):
        assert detect_note("Lead_Bb4")[1] == 70

    def test_no_note(self):
        assert detect_note("riser_big") is None


class TestVelocityDetection:
    def test_velocity_number(self):
        assert detect_velocity("Brass_v90") == 90
        assert detect_velocity("Pad_v127") == 127

    def test_velocity_words_extended(self):
        # New velocity vocabulary requested by user — word boundaries require
        # non-word separators (space / dash), not underscores.
        assert detect_velocity("note sustained") == 95
        assert detect_velocity("piano long held") == 90
        assert detect_velocity("hat-short") == 110
        assert detect_velocity("string stab") == 120
        assert detect_velocity("bass low") == 80
        assert detect_velocity("vox sistain") == 95

    def test_velocity_classic_dynamics(self):
        assert detect_velocity("piano pp") == 35
        assert detect_velocity("brass fff") == 127

    def test_velocity_none(self):
        assert detect_velocity("just_a_name") is None


class TestDefaultRoot:
    def test_bass_defaults_to_c2(self):
        note, midi, track = default_root_for("Bass808")
        assert note == "C2" and midi == 36 and track is True

    def test_fx_disables_pitch_tracking(self):
        _, _, track = default_root_for("FXRisers")
        assert track is False

    def test_bell_default_c5(self):
        note, midi, _ = default_root_for("DrillBells")
        assert note == "C5" and midi == 72


class TestCategoryTemplates:
    def test_drillbells_has_4_layers(self):
        tpl = category_template("DrillBells")
        assert len(tpl["layers"]) == 4
        assert tpl["layers"][0]["type"] == "sample"
        assert tpl["layers"][1]["type"] == "oscillator"
        assert "macros" in tpl
        assert len(tpl["macros"]) == 4

    def test_bass808_template_tuned_low(self):
        tpl = category_template("Bass808")
        assert tpl["layers"][0]["rootMidi"] == 36
        assert tpl["effects"]["saturation"]["enabled"] is True

    def test_fxrisers_no_pitch_tracking(self):
        tpl = category_template("FXRisers")
        assert tpl["layers"][0]["pitchTracking"] is False
        assert tpl["layers"][0]["oneShotMode"] is True
