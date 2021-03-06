/*
 * Wolf Shaper
 * Copyright (C) 2018 Patrick Desaulniers
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "DistrhoPlugin.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>

#include "WolfShaperParameters.hpp"
#include "Graph.hpp"
#include "Oversampler.hpp"
#include "ParamSmooth.hpp"

#include "DspFilters/Dsp.h"

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------------------------------------------

class WolfShaper : public Plugin
{
  public:


	WolfShaper() : Plugin(paramCount, 0, 1),
				   oversampler(),
				   removeDCPrev(0.0f)
	{
		parameters[paramPreGain] = ParamSmooth(1.0f);
		parameters[paramWet] = ParamSmooth(1.0f);
		parameters[paramPostGain] = ParamSmooth(1.0f);
		parameters[paramRemoveDC] = ParamSmooth(1.0f);
		parameters[paramOversample] = ParamSmooth(0.0f);
		parameters[paramBipolarMode] = ParamSmooth(0.0f);
		parameters[paramHorizontalWarpType] = ParamSmooth(0.0f);
		parameters[paramHorizontalWarpAmount] = ParamSmooth(0.0f);
		parameters[paramVerticalWarpType] = ParamSmooth(0.0f);
		parameters[paramVerticalWarpAmount] = ParamSmooth(0.0f);
		parameters[paramOut] = 0.0f;
	}

  protected:
	const char *getLabel() const noexcept override
	{
		return "Wolf Shaper";
	}

	const char *getDescription() const noexcept override
	{
		return "Waveshaping distortion plugin with spline-based graph.";
	}

	const char *getMaker() const noexcept override
	{
		return "Patrick Desaulniers";
	}

	const char *getHomePage() const noexcept override
	{
		return "https://github.com/pdesaulniers/wolf-shaper";
	}

	const char *getLicense() const noexcept override
	{
		return "GPL v3+";
	}

	uint32_t getVersion() const noexcept override
	{
		return d_version(0, 1, 0);
	}

	int64_t getUniqueId() const noexcept override
	{
		return d_cconst('s', 'W', 's', 'p');
	}

	void initParameter(uint32_t index, Parameter &parameter) override
	{
		switch (index)
		{
		case paramPreGain:
			parameter.name = "Pre Gain";
			parameter.symbol = "pregain";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 2.0f;
			parameter.ranges.def = 1.0f;
			parameter.hints = kParameterIsAutomable | kParameterIsLogarithmic;
			break;
		case paramWet:
			parameter.name = "Wet";
			parameter.symbol = "wet";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 1.0f;
			parameter.ranges.def = 1.0f;
			parameter.hints = kParameterIsAutomable;
			break;
		case paramPostGain:
			parameter.name = "Post Gain";
			parameter.symbol = "postgain";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 1.0f;
			parameter.ranges.def = 1.0f;
			parameter.hints = kParameterIsAutomable | kParameterIsLogarithmic;
			break;
		case paramRemoveDC:
			parameter.name = "Remove DC Offset";
			parameter.symbol = "removedc";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 1.0f;
			parameter.ranges.def = 1.0f;
			parameter.hints = kParameterIsAutomable | kParameterIsBoolean | kParameterIsInteger;
			break;
		case paramOversample:
			//None, 2x, 4x, 8x, 16x
			parameter.name = "Oversample";
			parameter.symbol = "oversample";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 4.0f;
			parameter.ranges.def = 0.0f;
			parameter.hints = kParameterIsAutomable | kParameterIsInteger;
			break;
		case paramBipolarMode:
			parameter.name = "Bipolar Mode";
			parameter.symbol = "bipolarmode";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 1.0f;
			parameter.ranges.def = 0.0f;
			parameter.hints = kParameterIsAutomable | kParameterIsBoolean | kParameterIsInteger;
			break;
		case paramHorizontalWarpType:
			//None, Bend +, Bend -, Bend +/-, Skew +, Skew -, Skew +/-
			parameter.name = "H Warp Type";
			parameter.symbol = "warptype";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 6.0f;
			parameter.ranges.def = 0.0f;
			parameter.hints = kParameterIsAutomable | kParameterIsInteger;
			break;
		case paramHorizontalWarpAmount:
			parameter.name = "H Warp Amount";
			parameter.symbol = "warpamount";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 1.0f;
			parameter.ranges.def = 0.0f;
			parameter.hints = kParameterIsAutomable;
			break;
		case paramVerticalWarpType:
			//None, Bend +, Bend -, Bend +/-, Skew +, Skew -, Skew +/-
			parameter.name = "V Warp Type";
			parameter.symbol = "vwarptype";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 6.0f;
			parameter.ranges.def = 0.0f;
			parameter.hints = kParameterIsAutomable | kParameterIsInteger;
			break;
		case paramVerticalWarpAmount:
			parameter.name = "V Warp Amount";
			parameter.symbol = "vwarpamount";
			parameter.ranges.min = 0.0f;
			parameter.ranges.max = 1.0f;
			parameter.ranges.def = 0.0f;
			parameter.hints = kParameterIsAutomable;
			break;
		case paramOut:
			parameter.name = "Out";
			parameter.symbol = "out";
			parameter.hints = kParameterIsOutput;
			break;
		}
	}

	float getParameterValue(uint32_t index) const override
	{
		return parameters[index].getRawValue();
	}

	void setParameterValue(uint32_t index, float value) override
	{
		parameters[index].setValue(value);
	}

	void initState(uint32_t index, String &stateKey, String &defaultStateValue) override
	{
		switch (index)
		{
		case 0:
			stateKey = "graph";
			break;
		}

		//generated with fprintf(stderr, "%A,%A,%A,%d;%A,%A,%A,%d;\n", 0.0f, 0.0f, 0.0f, wolf::CurveType::Exponential, 1.0f, 1.0f, 0.0f, wolf::CurveType::Exponential);
		defaultStateValue = String("0x0p+0,0x0p+0,0x0p+0,0;0x1p+0,0x1p+0,0x0p+0,0;");
	}

	void setState(const char *key, const char *value) override
	{
		if (std::strcmp(key, "graph") == 0)
		{
			lineEditor.rebuildFromString(value);
		}
	}

	float removeDCOffset(float input)
	{
		//Steep IIR filter at the DC frequency

		const float scaleFactor = 0.9999f; //Closer to 1 means steeper stop band

		const float value = input + scaleFactor * removeDCPrev;
		const float result = value - removeDCPrev;

		removeDCPrev = value;

		return result;
	}

	/* bool isSilence(const float **buffer, uint32_t frames)
	{
		for (uint32_t i = 0; i < frames; ++i)
		{
			if (buffer[0][i] != 0.0f || buffer[1][i] != 0.0f)
			{
				return false;
			}
		}

		return true;
	} */

	float calculateValueOutsideGraph(float value)
	{
		const bool bipolarMode = parameters[paramBipolarMode].getRawValue() > 0.50f;

		if (bipolarMode)
		{
			const bool positiveInput = value >= 0.0f;
			const float vertexY = lineEditor.getVertexAtIndex(positiveInput ? lineEditor.getVertexCount() - 1 : 0)->getY();
			const float absValue = std::abs(value);

			return absValue * (-1.0f + vertexY * 2.0f);
		}
		else
		{
			return value * lineEditor.getVertexAtIndex(lineEditor.getVertexCount() - 1)->getY();
		}
	}

	float getGraphValue(float input)
	{
		const float absInput = std::abs(input);

		if (absInput > 1.0f)
		{
			return calculateValueOutsideGraph(input);
		}

		const bool bipolarMode = parameters[paramBipolarMode].getRawValue() > 0.50f;

		if (bipolarMode)
		{
			const float x = (1.0f + input) * 0.5f;
			return -1.0f + lineEditor.getValueAt(x) * 2.0f;
		}
		else
		{
			return lineEditor.getValueAt(input);
		}
	}

	void run(const float **inputs, float **outputs, uint32_t frames) override
	{
		float max = 0.0f;

		const int oversamplingParameter = std::round(parameters[paramOversample].getRawValue());

		const int oversamplingRatio = std::pow(2, oversamplingParameter);
		uint32_t numSamples = frames * oversamplingRatio;

		const double sampleRate = getSampleRate();
		const float smoothFreq = 20.0f;

		float **buffer = oversampler.upsample(oversamplingRatio, frames, sampleRate, inputs);

		wolf::WarpType horizontalWarpType = (wolf::WarpType)std::round(parameters[paramHorizontalWarpType].getRawValue());
		lineEditor.setHorizontalWarpType(horizontalWarpType);

		wolf::WarpType verticalWarpType = (wolf::WarpType)std::round(parameters[paramVerticalWarpType].getRawValue());
		lineEditor.setVerticalWarpType(verticalWarpType);

		for (uint32_t i = 0; i < numSamples; ++i)
		{
			lineEditor.setHorizontalWarpAmount(parameters[paramHorizontalWarpAmount].getSmoothedValue(smoothFreq, sampleRate));
			lineEditor.setVerticalWarpAmount(parameters[paramVerticalWarpAmount].getSmoothedValue(smoothFreq, sampleRate));

			const float preGain = parameters[paramPreGain].getSmoothedValue(smoothFreq, sampleRate);

			float inputL = preGain * buffer[0][i];
			float inputR = preGain * buffer[1][i];

			//this hack makes sure that dc offset in the graph doesn't oscillate... hopefully
			if (inputL < 0.0f && inputL > -0.00001f)
			{
				inputL = 0.0f;
			}
			if (inputR < 0.0f && inputR > -0.00001f)
			{
				inputR = 0.0f;
			}

			const float absL = std::abs(inputL);
			const float absR = std::abs(inputR);

			max = std::max(max, absL);
			max = std::max(max, absR);

			const bool bipolarMode = parameters[paramBipolarMode].getRawValue() > 0.50f;
			lineEditor.setBipolarMode(bipolarMode);

			float graphL, graphR;

			graphL = getGraphValue(inputL);
			graphR = getGraphValue(inputR);

			const float wet = parameters[paramWet].getSmoothedValue(smoothFreq, sampleRate);
			const float dry = 1.0f - wet;
			const float postGain = parameters[paramPostGain].getSmoothedValue(smoothFreq, sampleRate);
			const bool mustRemoveDC = parameters[paramRemoveDC].getRawValue() > 0.50f;

			buffer[0][i] = (dry * inputL + wet * graphL) * postGain;
			buffer[1][i] = (dry * inputR + wet * graphR) * postGain;

			if (mustRemoveDC)
			{
				buffer[0][i] = removeDCOffset(buffer[0][i]);
				buffer[1][i] = removeDCOffset(buffer[1][i]);
			}
		}

		oversampler.downsample(outputs);

		setParameterValue(paramOut, max);
	}

  private:
	ParamSmooth parameters[paramCount];
	Oversampler oversampler;

	wolf::Graph lineEditor;

	float removeDCPrev;

	DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WolfShaper)
};

Plugin *createPlugin()
{
	return new WolfShaper();
}

// -----------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
