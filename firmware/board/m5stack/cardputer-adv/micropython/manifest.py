# The stock Adv keyboard uses TCA8418 and the official M5Stack MatrixKeyboard API.
freeze("$(MPY_DIR)/../m5stack/libs", ["driver/__init__.py", "driver/tca8418.py", "hardware/__init__.py",
                                     "hardware/matrix_keyboard.py", "hardware/keyboard/__init__.py", "hardware/keyboard/asciimap.py"])
