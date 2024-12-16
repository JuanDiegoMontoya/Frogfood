#include "Input.h"

#include "GLFW/glfw3.h"
#include "glm/vec3.hpp"
#include "imgui.h"

#include <thread>
#include <chrono>

InputSystem::InputSystem(GLFWwindow* window) : window_(window)
{
  glfwSetInputMode(window, GLFW_CURSOR, cursorIsActive ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void InputSystem::VariableUpdatePre(DeltaTime, World& world, bool swapchainOk)
{
  cursorFrameOffset = {0.0, 0.0};
  
  if (swapchainOk)
  {
    glfwPollEvents();
  }
  else
  {
    glfwWaitEvents();
    return;
  }

  glfwSetInputMode(window_, GLFW_CURSOR, cursorIsActive || world.GetRegistry().ctx().get<Debugging>().forceShowCursor ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

  if (!cursorIsActive)
  {
    if (world.GetRegistry().ctx().get<GameState>() == GameState::GAME)
    {
      for (auto&& [entity, input, inputLook, transform, gtransform] : world.GetRegistry().view<LocalPlayer, InputState, InputLookState, LocalTransform, GlobalTransform>().each())
      {
        input.forward += glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS ? 1 : 0;
        input.forward -= glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS ? 1 : 0;
        input.strafe += glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS ? 1 : 0;
        input.strafe -= glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS ? 1 : 0;
        input.elevate += glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS ? 1 : 0;
        input.elevate -= glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS ? 1 : 0;
        input.jump         = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS ? true : false;
        input.sprint       = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? true : false;
        input.walk         = glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ? true : false;
        input.usePrimary   = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS ? true : false;
        input.useSecondary = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_2) == GLFW_PRESS ? true : false;
        input.interact     = glfwGetKey(window_, GLFW_KEY_F) == GLFW_PRESS ? true : false;
        // angleAxis rotates clockwise if we are looking 'down' the axis (backwards). Keep in mind whether the coordinate system is LH or RH when doing this.
        inputLook.yaw -= static_cast<float>(cursorFrameOffset.x * 0.0025f);
        inputLook.pitch += static_cast<float>(cursorFrameOffset.y * 0.0025f);
        transform.rotation = glm::angleAxis(inputLook.yaw, glm::vec3{0, 1, 0}) * glm::angleAxis(inputLook.pitch, glm::vec3{1, 0, 0});
        gtransform.rotation = transform.rotation;
      }
    }
  }

  // Close the app if the user presses Escape.
  if (ImGui::GetKeyPressedAmount(ImGuiKey_Escape, 10000, 1))
  {
    auto& state = world.GetRegistry().ctx().get<GameState>();
    if (state == GameState::PAUSED)
    {
      state = GameState::GAME;
    }
    else if (state == GameState::GAME)
    {
      state = GameState::PAUSED;
    }
  }

  auto& debug = world.GetRegistry().ctx().get<Debugging>();
  if (ImGui::GetKeyPressedAmount(ImGuiKey_F1, 10000, 1))
  {
    debug.showDebugGui = !debug.showDebugGui;
  }

  if (ImGui::GetKeyPressedAmount(ImGuiKey_F2, 10000, 1))
  {
    debug.forceShowCursor = !debug.forceShowCursor;
  }

  // Sleep for a bit if the window is not focused
  if (!glfwGetWindowAttrib(window_, GLFW_FOCUSED))
  {
    // Use to render less- game will catch up
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(50));
  }

  // Toggle the cursor if the grave accent (tilde) key is pressed.
  if (glfwGetKey(window_, GLFW_KEY_GRAVE_ACCENT) && graveHeldLastFrame == false)
  {
    cursorIsActive          = !cursorIsActive;
    cursorJustEnteredWindow = true;
    graveHeldLastFrame      = true;
  }

  if (!glfwGetKey(window_, GLFW_KEY_GRAVE_ACCENT))
  {
    graveHeldLastFrame = false;
  }

  // Prevent the cursor from clicking ImGui widgets when it is disabled.
  if (!cursorIsActive)
  {
    glfwSetCursorPos(window_, 0, 0);
    cursorPos.x = 0;
    cursorPos.y = 0;
  }
}

void InputSystem::CursorPosCallback(double currentCursorX, double currentCursorY)
{
  if (cursorJustEnteredWindow)
  {
    cursorPos               = {currentCursorX, currentCursorY};
    cursorJustEnteredWindow = false;
  }

  cursorFrameOffset += glm::dvec2{currentCursorX - cursorPos.x, cursorPos.y - currentCursorY};
  cursorPos = {currentCursorX, currentCursorY};
}

void InputSystem::CursorEnterCallback(int entered)
{
  if (entered)
  {
    cursorJustEnteredWindow = true;
  }
}
