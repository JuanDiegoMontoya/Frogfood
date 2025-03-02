#include "Input.h"

#include "GLFW/glfw3.h"
#include "glm/vec3.hpp"
#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <thread>
#include <chrono>

InputSystem::InputSystem(GLFWwindow* window) : window_(window)
{
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  
  if (glfwRawMouseMotionSupported() == GLFW_TRUE)
  {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }
}

void InputSystem::VariableUpdatePre(DeltaTime, World& world, bool swapchainOk)
{
  const auto lastCursorMode = glfwGetInputMode(window_, GLFW_CURSOR);
  auto newCursorMode        = lastCursorMode;

  cursorFrameOffset = {0.0, 0.0};
  scrollOffset      = {0.0, 0.0};
  
  if (swapchainOk)
  {
    ZoneScopedN("glfwPollEvents");
    glfwPollEvents();
  }
  else
  {
    glfwWaitEvents();
    return;
  }

  if (newCursorMode == GLFW_CURSOR_DISABLED)
  {
    if (world.GetRegistry().ctx().get<GameState>() == GameState::GAME)
    {
      for (auto&& [entity, player, input, inputLook, transform, gtransform] : world.GetRegistry().view<Player, LocalPlayer, InputState, InputLookState, LocalTransform, GlobalTransform>().each())
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
        transform.rotation  = glm::angleAxis(inputLook.yaw, glm::vec3{0, 1, 0}) * glm::angleAxis(inputLook.pitch, glm::vec3{1, 0, 0});
        gtransform.rotation = transform.rotation;

        if (auto* i = world.GetRegistry().try_get<Inventory>(entity))
        {
          for (size_t j = 0; j < i->width; j++)
          {
            const auto currentSlotCoord = glm::ivec2(0, j);
            if (glfwGetKey(window_, GLFW_KEY_1 + (int)j) == GLFW_PRESS && j < i->width)
            {
              i->SetActiveSlot(currentSlotCoord, entity);
              break;
            }
          }

          if (scrollOffset.y != 0)
          {
            const auto offset = -(int)scrollOffset.y;
            const auto newCol = (int)glm::mod((float)i->activeSlotCoord.y + offset, (float)i->width);
            i->SetActiveSlot({0, newCol}, entity);
          }
        }
      }
    }
  }

  if (world.GetRegistry().ctx().get<GameState>() == GameState::GAME)
  {
    auto range = world.GetRegistry().view<Player, LocalPlayer>().each();

    if (range.begin() != range.end())
    {
      auto&& [e, p] = *range.begin();

      if (ImGui::GetKeyPressedAmount(ImGuiKey_Tab, 10000, 1))
      {
        p.inventoryIsOpen = !p.inventoryIsOpen;
      }

      if (p.inventoryIsOpen)
      {
        newCursorMode = GLFW_CURSOR_NORMAL;
      }
      else
      {
        newCursorMode = GLFW_CURSOR_DISABLED;
      }
    }
  }
  else // game state is not GAME
  {
    newCursorMode = GLFW_CURSOR_NORMAL;
  }

  auto& debug = world.GetRegistry().ctx().get<Debugging>();
  if (debug.forceShowCursor)
  {
    newCursorMode = GLFW_CURSOR_NORMAL;
  }

  if (newCursorMode != lastCursorMode)
  {
    glfwSetInputMode(window_, GLFW_CURSOR, newCursorMode);
  }

  // Prevent the cursor from clicking ImGui widgets when it is disabled.
  if (newCursorMode == GLFW_CURSOR_DISABLED)
  {
    ImGui::GetIO().MousePos = {0, 0};
    glfwSetCursorPos(window_, 0, 0);
    cursorPos.x = 0;
    cursorPos.y = 0;
  }

  if (ImGui::GetKeyPressedAmount(ImGuiKey_F1, 10000, 1))
  {
    debug.showDebugGui = !debug.showDebugGui;
  }

  if (ImGui::GetKeyPressedAmount(ImGuiKey_F2, 10000, 1))
  {
    debug.forceShowCursor = !debug.forceShowCursor;
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

  // Sleep for a bit if the window is not focused
  if (!glfwGetWindowAttrib(window_, GLFW_FOCUSED))
  {
    // Use to render less- game will catch up
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(50));
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

void InputSystem::ScrollCallback(double xOffset, double yOffset)
{
  scrollOffset.x += xOffset;
  scrollOffset.y += yOffset;
}
