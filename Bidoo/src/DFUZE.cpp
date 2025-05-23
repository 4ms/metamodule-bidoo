#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "gverb.h"
#include "dep/gverb/src/gverb.c"
#include "dep/gverb/src/gverbdsp.c"

using namespace std;

struct DFUZE : BidooModule {
	enum ParamIds {
		SIZE_PARAM,
		REVTIME_PARAM,
		DAMP_PARAM,
		FREEZE_PARAM,
		BANDWIDTH_PARAM,
		EARLYLEVEL_PARAM,
		TAIL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_INPUT,
		SIZE_INPUT,
		REVTIME_INPUT,
		DAMP_INPUT,
		FREEZE_INPUT,
		BANDWIDTH_INPUT,
		EARLYLEVEL_INPUT,
		TAIL_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};


	ty_gverb *verb;
	float lOut = 0.f, rOut = 0.f;

	DFUZE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SIZE_PARAM, 0.0f, 300.0f, 0.5f, "Size");
		configParam(REVTIME_PARAM, 0.0f, 50.0f, 0.5f, "Reverb time");
		configParam(DAMP_PARAM, 0.0f, 0.9f, 0.5f, "Damping");
		configParam(BANDWIDTH_PARAM, 0.0f, 1.0f, 0.5f, "Bandwidth");
    configParam(EARLYLEVEL_PARAM, 0.0f, 10.0f, 5.0f, "Early reflections level");
    configParam(TAIL_PARAM, 0.0f, 10.0f, 5.0f, "Tail level");

		verb = gverb_new(APP->engine->getSampleRate(), 300, 1, 1, 1, 1, 1, 1, 1);

		configInput(IN_INPUT, "In");
		configInput(SIZE_INPUT, "Size");
		configInput(REVTIME_INPUT, "Reverb time");
		configInput(DAMP_INPUT, "Damping");
		configInput(BANDWIDTH_INPUT, "Bandwidth");
		configInput(EARLYLEVEL_INPUT, "Early reflections level");
		configInput(TAIL_INPUT, "Tail level");

		configOutput(OUT_L_OUTPUT, "Out L");
		configOutput(OUT_R_OUTPUT, "Out R");
	}

	~DFUZE() {
		gverb_free(verb);
	}

	void process(const ProcessArgs &args) override;
};

void DFUZE::process(const ProcessArgs &args) {
	gverb_set_roomsize(verb, clamp(params[SIZE_PARAM].getValue()+rescale(inputs[SIZE_INPUT].getVoltage(),0.0f,10.0f,0.0f,300.0f),0.0f,300.0f));
	gverb_set_revtime(verb, clamp(params[REVTIME_PARAM].getValue()+rescale(inputs[REVTIME_INPUT].getVoltage(),0.0f,10.0f,0.0f,50.0f),0.0f,50.0f));
	gverb_set_damping(verb, clamp(params[DAMP_PARAM].getValue()+inputs[DAMP_INPUT].getVoltage(),0.0f,0.9f));
	gverb_set_inputbandwidth(verb, clamp(params[BANDWIDTH_PARAM].getValue()+inputs[BANDWIDTH_INPUT].getVoltage(),0.0f,1.0f));
	gverb_set_earlylevel(verb, clamp(rescale(params[EARLYLEVEL_PARAM].getValue()+inputs[EARLYLEVEL_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f),0.0f,1.0f));
	gverb_set_taillevel(verb, clamp(rescale(params[TAIL_PARAM].getValue()+inputs[TAIL_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f),0.0f,1.0f));

	gverb_do(verb, inputs[IN_INPUT].getVoltage()/10.0f, &lOut, &rOut);
	outputs[OUT_L_OUTPUT].setVoltage(lOut);
	outputs[OUT_R_OUTPUT].setVoltage(rOut);
}

struct DFUZEWidget : BidooWidget {
	DFUZEWidget(DFUZE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/DFUZE.svg"));

		addParam(createParam<BidooBlueKnob>(Vec(13, 50), module, DFUZE::SIZE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 95), module, DFUZE::REVTIME_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 140), module, DFUZE::DAMP_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 185), module, DFUZE::BANDWIDTH_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 230), module, DFUZE::EARLYLEVEL_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 275), module, DFUZE::TAIL_PARAM));


		addInput(createInput<PJ301MPort>(Vec(65.0f, 52.0f), module, DFUZE::SIZE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 97.0f), module, DFUZE::REVTIME_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 142.0f), module, DFUZE::DAMP_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 187.0f), module, DFUZE::BANDWIDTH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 232.0f), module, DFUZE::EARLYLEVEL_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 277.0f), module, DFUZE::TAIL_INPUT));

	 	//Changed ports opposite way around
		addInput(createInput<PJ301MPort>(Vec(7.0f, 330.0f), module, DFUZE::IN_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f, 340.0f), module, DFUZE::OUT_L_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f+22.0f, 340.0f), module, DFUZE::OUT_R_OUTPUT));
	}
};

Model *modelDFUZE = createModel<DFUZE, DFUZEWidget>("dFUZE");
