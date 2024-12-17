#pragma once
#include "Game.h"
#include "glm/vec2.hpp"

#include <variant>
#include <unordered_map>

// Head-only

enum class KeyboardButton
{
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  ZERO, ONE, TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, MINUS, PLUS, GRAVE_ACCENT,
  LEFT_BRACE, RIGHT_BRACE, SEMICOLON, APOSTROPHE, COMMA, PERIOD, FORWARD_SLASH, BACK_SLASH,
  INSERT, DELETE, HOME, END, PAGE_UP, PAGE_DOWN, LEFT, RIGHT, UP, DOWN,
  KP_0, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8, KP_9, KP_DIVIDE, KP_TIMES, KP_PLUS, KP_MINUS, KP_DOT, KP_ENTER,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

enum class MouseButton
{
  
};

class InputSystem
{
public:
  NO_COPY_NO_MOVE(InputSystem);
  InputSystem(struct GLFWwindow* window);
  void VariableUpdatePre(DeltaTime dt, World& world, bool swapchainOk);

  void CursorPosCallback(double currentCursorX, double currentCursorY);
  void CursorEnterCallback(int entered);

private:
  GLFWwindow* window_;
  glm::dvec2 cursorPos{};
  glm::dvec2 cursorFrameOffset{};
  bool cursorJustEnteredWindow = true;
};
