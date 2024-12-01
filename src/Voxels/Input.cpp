#include "Input.h"

#include "GLFW/glfw3.h"
#include "glm/vec3.hpp"

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

  if (!cursorIsActive)
  {
    if (world.GetRegistry().ctx().get<GameState>() == GameState::GAME)
    {
      for (auto&& [entity, player, input, inputLook, transform] : world.GetRegistry().view<Player, InputState, InputLookState, Transform>().each())
      {
        // TODO: Client should store the ID of the player that they own, then check against that.
        if (player.id == 0)
        {
          input.forward += glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS ? 1 : 0;
          input.forward -= glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS ? 1 : 0;
          input.strafe += glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS ? 1 : 0;
          input.strafe -= glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS ? 1 : 0;
          input.elevate += glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS ? 1 : 0;
          input.elevate -= glfwGetKey(window_, GLFW_KEY_Q) == GLFW_PRESS ? 1 : 0;
          input.sprint = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? true : false;
          input.walk = glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ? true : false;
          inputLook.yaw += static_cast<float>(cursorFrameOffset.x * 0.0025f);
          inputLook.pitch -= static_cast<float>(cursorFrameOffset.y * 0.0025f); // Subtract due to rendering hack that flips the camera
          transform.rotation = glm::angleAxis(inputLook.yaw, glm::vec3{0, 1, 0}) * glm::angleAxis(inputLook.pitch, glm::vec3{1, 0, 0});
          ;
          break;
        }
      }
    }
  }

  // Close the app if the user presses Escape.
  if (glfwGetKey(window_, GLFW_KEY_ESCAPE))
  {
    glfwSetWindowShouldClose(window_, true);
  }

  // Sleep for a bit if the window is not focused
  if (!glfwGetWindowAttrib(window_, GLFW_FOCUSED))
  {
    // TODO: Use sleep_until so we don't skip game ticks
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(50));
  }

  // Toggle the cursor if the grave accent (tilde) key is pressed.
  if (glfwGetKey(window_, GLFW_KEY_GRAVE_ACCENT) && graveHeldLastFrame == false)
  {
    cursorIsActive          = !cursorIsActive;
    cursorJustEnteredWindow = true;
    graveHeldLastFrame      = true;
    glfwSetInputMode(window_, GLFW_CURSOR, cursorIsActive ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
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
