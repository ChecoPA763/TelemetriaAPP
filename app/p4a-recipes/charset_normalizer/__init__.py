"""Receta Android pura para evitar ruedas nativas incompatibles con p4a."""

from pythonforandroid.recipe import PythonRecipe


class CharsetNormalizerRecipe(PythonRecipe):
    version = "3.4.0"
    url = (
        "https://files.pythonhosted.org/packages/source/c/charset-normalizer/"
        "charset_normalizer-{version}.tar.gz"
    )
    site_packages_name = "charset_normalizer"
    depends = ["setuptools"]
    call_hostpython_via_targetpython = False

    def get_recipe_env(self, arch=None, with_flags_in_cc=True):
        env = super().get_recipe_env(arch, with_flags_in_cc)
        # La implementacion Python es suficiente; no compilar la extension mypyc.
        env["CHARSET_NORMALIZER_USE_MYPYC"] = "0"
        return env


recipe = CharsetNormalizerRecipe()

