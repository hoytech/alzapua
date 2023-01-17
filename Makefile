BIN = alzapua
OPT = -O3 -g

include golpe/rules.mk

INCS += -Iexternal/imgui -Iexternal/imgui/backends/
SRCS += external/imgui/imgui.cpp external/imgui/imgui_draw.cpp external/imgui/imgui_widgets.cpp external/imgui/imgui_tables.cpp external/imgui/backends/imgui_impl_glfw.cpp external/imgui/backends/imgui_impl_opengl3.cpp
LDLIBS += -lGL -lGLEW -lglfw -lrt -lm
