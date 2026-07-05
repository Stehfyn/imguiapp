// Animation builtin controls (F56): dt-driven Task-phase animators, each a compiled ImGuiAppControl with a
// typed PersistData DataOut consumed downstream in dependency order. Registered into the Composer via
// AppGraphAddBuiltin (the RandomTime precedent). OnUpdate is the SOLE mutator and the whole accumulator lives
// in PersistData -- ImGuiAppStateHistory snapshot/restore then reproduces every trajectory byte-for-byte under
// Fixed-dt, so App-time scrub restores an animator's value. No TU globals, no static carry. Triggers obey the
// temp^last rising-edge idiom.
#pragma once

#include "imguiapp.h"

// Ease selector for ImAppTween.
enum ImAppEase_
{
  ImAppEase_Linear = 0,
  ImAppEase_Smoothstep,
};

// Interpolation curve on t in [0,1].
static inline float ImAppEase(int ease, float t)
{
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return ease == ImAppEase_Smoothstep ? t * t * (3.0f - 2.0f * t) : t;
}

//-----------------------------------------------------------------------------
// Tween: eases a->b over duration seconds; restarts on a rising trigger.
//-----------------------------------------------------------------------------
struct ImAppTweenData
{
  float a;         // start value (param)
  float b;         // end value (param)
  float duration;  // seconds (param)
  int   ease;      // ImAppEase_ (param)
  float t;         // accumulator [0,1]
  float value;     // DataOut: eased a->b at t
  bool  done;      // DataOut: t reached 1
};
struct ImAppTweenTempData
{
  bool trigger;    // rising edge restarts
};
struct ImAppTween : ImGuiAppControl<ImAppTweenData, ImAppTweenTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppTweenData* data) const override final
  {
    IM_UNUSED(app);
    data->a = 0.0f;
    data->b = 1.0f;
    data->duration = 1.0f;
    data->ease = ImAppEase_Linear;
    data->t = 0.0f;
    data->value = data->a;
    data->done = false;
  }

  virtual void OnUpdate(float dt, ImAppTweenData* data, const ImAppTweenTempData* temp_data, const ImAppTweenTempData* last_temp_data) const override final
  {
    if (temp_data->trigger && !last_temp_data->trigger)   // rising: restart
    {
      data->t = 0.0f;
      data->done = false;
    }
    data->t += data->duration > 0.0f ? dt / data->duration : 1.0f;
    if (data->t > 1.0f)
      data->t = 1.0f;
    data->value = data->a + (data->b - data->a) * ImAppEase(data->ease, data->t);
    data->done = data->t >= 1.0f;
  }
};

//-----------------------------------------------------------------------------
// Timer: counts elapsed seconds; done latches at duration; restarts on a rising trigger.
//-----------------------------------------------------------------------------
struct ImAppTimerData
{
  float duration;  // seconds (param)
  float elapsed;   // accumulator
  bool  done;      // DataOut: elapsed >= duration
};
struct ImAppTimerTempData
{
  bool trigger;    // rising edge restarts
};
struct ImAppTimer : ImGuiAppControl<ImAppTimerData, ImAppTimerTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppTimerData* data) const override final
  {
    IM_UNUSED(app);
    data->duration = 1.0f;
    data->elapsed = 0.0f;
    data->done = false;
  }

  virtual void OnUpdate(float dt, ImAppTimerData* data, const ImAppTimerTempData* temp_data, const ImAppTimerTempData* last_temp_data) const override final
  {
    if (temp_data->trigger && !last_temp_data->trigger)   // rising: restart
    {
      data->elapsed = 0.0f;
      data->done = false;
    }
    data->elapsed += dt;
    data->done = data->elapsed >= data->duration;
  }
};

//-----------------------------------------------------------------------------
// Spring: damped-harmonic integration toward target; {value,velocity} is the accumulator.
//-----------------------------------------------------------------------------
struct ImAppSpringData
{
  float target;     // param
  float stiffness;  // param (k)
  float damping;    // param (c)
  float value;      // DataOut
  float velocity;   // accumulator
};
struct ImAppSpringTempData
{
};
struct ImAppSpring : ImGuiAppControl<ImAppSpringData, ImAppSpringTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppSpringData* data) const override final
  {
    IM_UNUSED(app);
    data->target = 1.0f;
    data->stiffness = 8.0f;
    data->damping = 2.0f;
    data->value = 0.0f;
    data->velocity = 0.0f;
  }

  virtual void OnUpdate(float dt, ImAppSpringData* data, const ImAppSpringTempData* temp_data, const ImAppSpringTempData* last_temp_data) const override final
  {
    IM_UNUSED(temp_data);
    IM_UNUSED(last_temp_data);
    data->velocity += (data->stiffness * (data->target - data->value) - data->damping * data->velocity) * dt;
    data->value += data->velocity * dt;
  }
};

//-----------------------------------------------------------------------------
// Pulse: free-running phase in [0,1); pulse is a one-frame flag on each wrap.
//-----------------------------------------------------------------------------
struct ImAppPulseData
{
  float period;   // seconds per cycle (param)
  float phase;    // accumulator [0,1)
  bool  pulse;    // DataOut: one-frame wrap flag
};
struct ImAppPulseTempData
{
};
struct ImAppPulse : ImGuiAppControl<ImAppPulseData, ImAppPulseTempData>
{
  virtual void OnInitialize(ImGuiApp* app, ImAppPulseData* data) const override final
  {
    IM_UNUSED(app);
    data->period = 1.0f;
    data->phase = 0.0f;
    data->pulse = false;
  }

  virtual void OnUpdate(float dt, ImAppPulseData* data, const ImAppPulseTempData* temp_data, const ImAppPulseTempData* last_temp_data) const override final
  {
    IM_UNUSED(temp_data);
    IM_UNUSED(last_temp_data);
    data->phase += data->period > 0.0f ? dt / data->period : 0.0f;
    if (data->phase >= 1.0f)
    {
      data->phase -= 1.0f;
      data->pulse = true;
    }
    else
    {
      data->pulse = false;
    }
  }
};
