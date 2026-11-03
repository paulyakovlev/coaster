# Adafruit GFX ships drivers for SPI and I2C displays, which need Arduino
# hardware libraries. The simulator only needs the core drawing code, so
# leave those two files out of the native build.

Import("env")


def skip(env, node):
    return None


env.AddBuildMiddleware(skip, "*/Adafruit_SPITFT.cpp")
env.AddBuildMiddleware(skip, "*/Adafruit_GrayOLED.cpp")
