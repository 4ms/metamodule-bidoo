#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include <vector>
#include "cmath"
#include <iomanip>
// #include <sstream>
#include <algorithm>
#include <atomic>
#include "dep/waves.hpp"
#include "../debug_raw.h"

#if defined(METAMODULE)
#include "async_filebrowser.hh"
#include "CoreModules/async_thread.hh"
#endif

using namespace std;

struct CANARD : BidooModule {
	enum ParamIds {
		RECORD_PARAM,
		SAMPLE_START_PARAM,
		LOOP_LENGTH_PARAM,
		READ_MODE_PARAM,
		SPEED_PARAM,
		FADE_PARAM,
		MODE_PARAM,
		SLICE_PARAM,
		CLEAR_PARAM,
		THRESHOLD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INL_INPUT,
		INR_INPUT,
		TRIG_INPUT,
		GATE_INPUT,
		SAMPLE_START_INPUT,
		LOOP_LENGTH_INPUT,
		READ_MODE_INPUT,
		SPEED_INPUT,
		RECORD_INPUT,
		FADE_INPUT,
		SLICE_INPUT,
		CLEAR_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		REC_LIGHT,
		NUM_LIGHTS
	};

	bool play = false;
	bool record = false;
	bool save = false;
	int channels = 2;
	int sampleRate = 0;
	int totalSampleCount = 0;
	vector<dsp::Frame<2>> playBuffer, recordBuffer;
	float samplePos = 0.0f, sampleStart = 0.0f, loopLength = 0.0f, fadeLenght = 0.0f, fadeCoeff = 1.0f, speedFactor = 1.0f;
	size_t prevPlayedSlice = 0;
	size_t playedSlice = 0;
	bool changedSlice = false;
	int readMode = 0; // 0 formward, 1 backward, 2 repeat
	float speed;
	std::vector<int> slices;
	int selected = -1;
	bool deleteFlag = false;
	int addSliceMarker = -1;
	bool addSliceMarkerFlag = false;
	int deleteSliceMarker = -1;
	bool deleteSliceMarkerFlag = false;
	size_t index = 0;
	float prevGateState = 0.0f;
	float prevTrigState = 0.0f;
	std::string lastPath;
	std::string waveFileName;
	std::string waveExtension;
	std::atomic<bool> loading = false;
	bool clear_requested = false;
	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger recordTrigger;
	dsp::SchmittTrigger clearTrigger;
	dsp::PulseGenerator eocPulse;
	std::atomic<bool> locked{false};
	bool newStop = false;
	bool first=true;

#if defined(METAMODULE)
	MetaModule::AsyncThread loadSampleAsync{this, [this]() {
		DebugPin3High();
		if (loading.load(std::memory_order_acquire)) {
			this->loadSampleInternal();
		}
		DebugPin3Low();
	}};
	
	MetaModule::AsyncThread saveSampleAsync{this, [this]() {
		this->saveSampleInternal();
	}};
#endif

	CANARD() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(RECORD_PARAM, 0.0f, 1.0f, 0.0f, "Record");
		configParam(SAMPLE_START_PARAM, 0.0f, 10.0f, 0.0f, "Sample start");
		configParam(LOOP_LENGTH_PARAM, 0.0f, 10.0f, 10.0f, "Loop length");
		configParam(READ_MODE_PARAM, 0.0f, 2.0f, 0.0f, "Read mode");
		configParam(SPEED_PARAM, -4.0f, 4.0f, 1.0f, "Speed");
		configParam(FADE_PARAM, 0.0f, 10.0f, 0.0f, "Fade");
		configParam(SLICE_PARAM, 0.0f, 10.0f, 0.0f, "Slice");
		configParam(CLEAR_PARAM, 0.0f, 1.0f, 0.0f, "Clear");
		configParam(THRESHOLD_PARAM, 0.01f, 10.0f, 1.0f, "Threshold");
		configSwitch(MODE_PARAM, 0, 1, 0, "Slice mode", {"Off", "On"});

		playBuffer.resize(0);
		recordBuffer.resize(0);

		configInput(INL_INPUT, "In L");
		configInput(INR_INPUT, "In R");
		configInput(TRIG_INPUT, "Trigger");
		configInput(GATE_INPUT, "Gate");
		configInput(SAMPLE_START_INPUT, "Sample start");
		configInput(LOOP_LENGTH_INPUT, "Loop length");
		configInput(READ_MODE_INPUT, "Read mode");
		configInput(SPEED_INPUT, "Speed");
		configInput(RECORD_INPUT, "Record");
		configInput(FADE_INPUT, "Fade");
		configInput(SLICE_INPUT, "Slice");
		configInput(CLEAR_INPUT, "Clear");

		configOutput(OUTL_OUTPUT, "Out L");
		configOutput(OUTR_OUTPUT, "Out R");
		configOutput(EOC_OUTPUT, "EOC");

#if defined(METAMODULE)
		loadSampleAsync.start();
		printf("CANARD: module is %p\n", this);
#endif
	}

	void process(const ProcessArgs &args) override;

	void calcLoop();
	void initPos();
	void loadSample();
	void saveSample();
	void loadSampleInternal();
	void saveSampleInternal();
	void calcTransients();

	void lock() {
		bool expected = false;
		while (!locked.compare_exchange_strong(expected, true)) {
			expected = false;
		}
	}

	bool try_lock() {
		bool expected = false;
		return locked.compare_exchange_strong(expected, true);
	}

	void unlock() {
		locked.store(false);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		// lastPath
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_t *slicesJ = json_array();
		for (size_t i = 0; i<slices.size() ; i++) {
			json_t *sliceJ = json_integer(slices[i]);
			json_array_append_new(slicesJ, sliceJ);
		}
		json_object_set_new(rootJ, "slices", slicesJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		printf("CANARD::dataFromJson\n");
		BidooModule::dataFromJson(rootJ);
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			waveFileName = rack::system::getFilename(lastPath);
			waveExtension = rack::system::getExtension(lastPath);
			if (!lastPath.empty()) loadSampleInternal();
			if (totalSampleCount>0) {
				json_t *slicesJ = json_object_get(rootJ, "slices");
				if (slicesJ) {
					size_t i;
					json_t *sliceJ;
					json_array_foreach(slicesJ, i, sliceJ) {
							if (i != 0)
								slices.push_back(json_integer_value(sliceJ));
					}
				}
			}
		}
	}

	void onSampleRateChange() override {
		if (!lastPath.empty()) loadSample();
	}
};

void CANARD::calcTransients() {
	slices.clear();
	slices.push_back(0);
	int i = 0;
	int size = 256;
	vector<dsp::Frame<2>>::const_iterator first;
	vector<dsp::Frame<2>>::const_iterator last;
	float prevNrgy = 0.0f;
	while (i+size<totalSampleCount) {
		first = playBuffer.begin() + i;
		last = playBuffer.begin() + i + size;
		vector<dsp::Frame<2>> newVec(first, last);
		float nrgy = 0.0f;
		float zcRate = 0.0f;
		unsigned int zcIdx = 0;
		bool first = true;
		for (int k = 0; k < size; k++) {
			nrgy += 100*newVec[k].samples[0]*newVec[k].samples[0]/size;
			if (newVec[k].samples[0]==0.0f) {
				zcRate += 1;
				if (first) {
					zcIdx = k;
					first = false;
				}
			}
		}
		if ((nrgy > params[CANARD::THRESHOLD_PARAM].getValue()) && (nrgy > 10*prevNrgy))
			slices.push_back(i+zcIdx);
		i+=size;
		prevNrgy = nrgy;
	}
}

void CANARD::loadSampleInternal() {
	// On MM, this is called during audio startup (dataFromJson)
	// and in the AsyncThread context when loading == true

	APP->engine->yieldWorkers();
	
	// Get extension and validate
	std::string ext = rack::system::getExtension(lastPath);
	std::string upperExt = rack::string::uppercase(ext);
	if (upperExt != ".WAV" && upperExt != ".AIFF") {
		// Invalid format - don't try to load
		lastPath = "";
		waveFileName = "";
		waveExtension = "";
		loading = false;
		return;
	}
	
#if defined(METAMODULE)
	// Try to get the lock from the GUI thread. If we fail (i.e. GUI thread is using the buffer),
	// then `loading` will stay true so we'll try again the next time the AsyncThread is scheduled.
	if (!try_lock())
		return;
#else
	lock();
#endif

	playBuffer = waves::getStereoWav(lastPath, APP->engine->getSampleRate(), waveFileName, waveExtension, channels, sampleRate, totalSampleCount);
	vector<dsp::Frame<2>>(playBuffer).swap(playBuffer);

	slices.clear();

	// unlock must be done after we're done accessing playBuffer and slices
	unlock();

	loading = false;
}

void CANARD::loadSample() {
	// On MM, this is only called from the audio context

#if !defined(METAMODULE)
	loadSampleInternal();
#endif
}

void CANARD::saveSampleInternal() {
	APP->engine->yieldWorkers();

#if defined(METAMODULE)
	if (!try_lock())
		return;
#else
	lock();
#endif

	waves::saveWave(playBuffer, APP->engine->getSampleRate(), lastPath);
	unlock();

	save = false;
}

void CANARD::saveSample() {
#if defined(METAMODULE)
	if (!saveSampleAsync.is_enabled())
		saveSampleAsync.run_once();
#else
	saveSampleInternal();
#endif
}

void CANARD::calcLoop() {
	prevPlayedSlice = index;
	index = 0;
	int sliceStart = 0;;
	int sliceEnd = totalSampleCount > 0 ? totalSampleCount - 1 : 0;
	if ((params[MODE_PARAM].getValue() == 1) && (slices.size()>0))
	{
		index = round(clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getVoltage(), 0.0f,10.0f)*(slices.size()-1)/10);
		sliceStart = slices[index];
		sliceEnd = (index < (slices.size() - 1)) ? (slices[index+1] - 1) : (totalSampleCount - 1);
	}

	if (totalSampleCount > 0) {
		sampleStart = rescale(clamp(inputs[SAMPLE_START_INPUT].getVoltage() + params[SAMPLE_START_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, sliceStart, sliceEnd);
		loopLength = clamp(rescale(clamp(inputs[LOOP_LENGTH_INPUT].getVoltage() + params[LOOP_LENGTH_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, sliceEnd - sliceStart + 1),1.0f,sliceEnd-sampleStart+1);
		fadeLenght = rescale(clamp(inputs[FADE_INPUT].getVoltage() + params[FADE_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f,0.0f, floor(loopLength/2));
	}
	else {
		loopLength = 0;
		sampleStart = 0;
		fadeLenght = 0;
	}
	playedSlice = index;
}

void CANARD::initPos() {
	if ((inputs[SPEED_INPUT].getVoltage() + params[SPEED_PARAM].getValue())>=0)
	{
		samplePos = sampleStart;
	}
	else
	{
		samplePos = sampleStart + loopLength;
	}
	speedFactor = 1.0f;
}

void CANARD::process(const ProcessArgs &args) {
#if !defined(METAMODULE)
	if (loading) {
		loadSample();
	}

	if (save) {
		saveSample();
	}
#endif

	if (clearTrigger.process(inputs[CLEAR_INPUT].getVoltage() + params[CLEAR_PARAM].getValue()))
	{
		clear_requested = true;
	}

	if (clear_requested)
	{
#if defined(METAMODULE)
		if (try_lock()) {
#else
		lock();
#endif
		playBuffer.clear();
		totalSampleCount = 0;
		slices.clear();
#if defined(METAMODULE)
		}
#endif
		unlock();
		lastPath = "";
		waveFileName = "";
		waveExtension = "";
		clear_requested = false;
	}

	if ((selected>=0) && (deleteFlag)) {
		int nbSample=0;
		if ((size_t)selected<(slices.size()-1)) {
			nbSample = slices[selected + 1] - slices[selected] - 1;
			lock();
			playBuffer.erase(playBuffer.begin() + slices[selected], playBuffer.begin() + slices[selected + 1]-1);
			unlock();
		}
		else {
			nbSample = totalSampleCount - slices[selected];
			lock();
			playBuffer.erase(playBuffer.begin() + slices[selected], playBuffer.end());
			unlock();
		}
		slices.erase(slices.begin()+selected);
		totalSampleCount = playBuffer.size();
		for (size_t i = selected; i < slices.size(); i++)
		{
			slices[i] = slices[i]-nbSample;
		}
		selected = -1;
		deleteFlag = false;
		calcLoop();
	}

	if ((addSliceMarker>=0) && (addSliceMarkerFlag)) {
		if (std::find(slices.begin(), slices.end(), addSliceMarker) != slices.end()) {
			addSliceMarker = -1;
			addSliceMarkerFlag = false;
		}
		else {
			auto it = std::upper_bound(slices.begin(), slices.end(), addSliceMarker);
			lock();
			slices.insert(it, addSliceMarker);
			unlock();
			addSliceMarker = -1;
			addSliceMarkerFlag = false;
			calcLoop();
		}
	}

	if ((deleteSliceMarker>=0) && (deleteSliceMarkerFlag)) {
		if (std::find(slices.begin(), slices.end(), deleteSliceMarker) != slices.end()) {
			lock();
			slices.erase(std::find(slices.begin(), slices.end(), deleteSliceMarker));
			unlock();
			deleteSliceMarker = -1;
			deleteSliceMarkerFlag = false;
			calcLoop();
		}
	}

	if (recordTrigger.process(inputs[RECORD_INPUT].getVoltage() + params[RECORD_PARAM].getValue()))
	{
		if(record) {
			if (floor(params[MODE_PARAM].getValue()) == 0) {
				lock();
				slices.clear();
				slices.push_back(0);
				playBuffer.resize(0);
				for (int i = 0; i < (int)recordBuffer.size(); i++) {
					dsp::Frame<2> frame = recordBuffer[i];
					playBuffer.push_back(frame);
				}
				totalSampleCount = playBuffer.size();
				unlock();
				lastPath = "";
				waveFileName = "";
				waveExtension = "";
			}
			else {
				lock();
				slices.push_back(totalSampleCount > 0 ? (totalSampleCount-1) : 0);
				playBuffer.insert(playBuffer.end(), recordBuffer.begin(), recordBuffer.end());
				totalSampleCount = playBuffer.size();
				unlock();
			}
			lock();
			recordBuffer.resize(0);
			unlock();
			lights[REC_LIGHT].setBrightness(0.0f);
		}
		record = !record;
	}

	if (record) {
		lights[REC_LIGHT].setBrightness(10.0f);
		lock();
		dsp::Frame<2> frame;
		frame.samples[0] = inputs[INL_INPUT].getVoltage()/10.0f;
		frame.samples[1] = inputs[INR_INPUT].getVoltage()/10.0f;
		recordBuffer.push_back(frame);
		unlock();
	}

	int trigMode = inputs[TRIG_INPUT].isConnected() ? 1 : (inputs[GATE_INPUT].isConnected() ? 2 : 0);
	int readMode = round(clamp(inputs[READ_MODE_INPUT].getVoltage() + params[READ_MODE_PARAM].getValue(),0.0f,2.0f));
	speed = inputs[SPEED_INPUT].getVoltage() + params[SPEED_PARAM].getValue();
	calcLoop();

	if (trigMode == 1) {
		if (trigTrigger.process(inputs[TRIG_INPUT].getVoltage()) && (prevTrigState == 0.0f))
		{
			initPos();
			if ((slices.size() == 1) && (inputs[SLICE_INPUT].isConnected())) {
				samplePos = sampleStart + loopLength * rescale(clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,1.0f);
			}
			play = true;
		}
		else {
			if ((readMode == 0) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
				play = false;
				if (newStop) {
					eocPulse.trigger(10 / args.sampleRate);
					newStop = false;
				}
			}
			else if ((readMode == 0) && (speed<0) && (samplePos == sampleStart)) {
				play = false;
				if (newStop) {
					eocPulse.trigger(10 / args.sampleRate);
					newStop = false;
				}
			}
			else if ((readMode == 1) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
				initPos();
			}
			else if ((readMode == 1) && (speed<0) && (samplePos == sampleStart)) {
				initPos();
			}
			else if ((readMode == 2) && ((samplePos == (sampleStart)) || (samplePos == (sampleStart+loopLength)))) {
				speedFactor = -1 * speedFactor;
				samplePos = samplePos + speedFactor * speed;
			}
			else {
				samplePos = samplePos + speedFactor * speed;
			}
		}
		samplePos = clamp(samplePos,sampleStart,sampleStart+loopLength);
	}
	else if (trigMode == 2)
	{
		if (inputs[GATE_INPUT].getVoltage()>0.1f)
		{
			play = true;
			if (inputs[SLICE_INPUT].isConnected()) {
				play = true;
				samplePos= sampleStart + rescale(clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getVoltage(), 0.0f,10.0f),0.0f,10.0f,0.0f,1.0f) * loopLength;
			}
			else if (prevGateState == 0.0f) {
				initPos();
			}
			else {
				if ((readMode == 0) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
					play = false;
					if (newStop) {
						eocPulse.trigger(10 / args.sampleRate);
						newStop = false;
					}
				}
				else if ((readMode == 0) && (speed<0) && (samplePos == sampleStart)) {
					play = false;
					if (newStop) {
						eocPulse.trigger(10 / args.sampleRate);
						newStop = false;
					}
				}
				else if ((readMode == 1) && (speed>=0) && (samplePos == (sampleStart+loopLength))) {
					initPos();
				}
				else if ((readMode == 1) && (speed<0) && (samplePos == sampleStart)) {
					initPos();
				}
				else if ((readMode == 2) && ((samplePos == (sampleStart)) || (samplePos == (sampleStart+loopLength)))) {
					speedFactor = -1 * speedFactor;
					samplePos = samplePos + speedFactor * speed;
				}
				else {
					samplePos = samplePos + speedFactor * speed;
				}
			}
			samplePos = clamp(samplePos,sampleStart,sampleStart+loopLength);
		}
		else {
			play = false;
		}
	}
	prevGateState = inputs[GATE_INPUT].getVoltage();
	prevTrigState = inputs[TRIG_INPUT].getVoltage();

	if (play) {
		newStop = true;
		if (samplePos<totalSampleCount) {
			if (fadeLenght>1000) {
				if ((samplePos-sampleStart)<fadeLenght)
					fadeCoeff = rescale(samplePos-sampleStart,0.0f,fadeLenght,0.0f,1.0f);
				else if ((sampleStart+loopLength-samplePos)<fadeLenght)
					fadeCoeff = rescale(sampleStart+loopLength-samplePos,fadeLenght,0.0f,1.0f,0.0f);
				else
					fadeCoeff = 1.0f;
			}
			else
				fadeCoeff = 1.0f;

			int xi = samplePos;
			float xf = samplePos - xi;
			float crossfaded = crossfade(playBuffer[xi].samples[0], playBuffer[min(xi + 1,(int)totalSampleCount-1)].samples[0], xf);
			outputs[OUTL_OUTPUT].setVoltage(crossfaded*fadeCoeff*5.0f);
			crossfaded = crossfade(playBuffer[xi].samples[1], playBuffer[min(xi + 1,(int)totalSampleCount-1)].samples[1], xf);
			outputs[OUTR_OUTPUT].setVoltage(crossfaded*fadeCoeff*5.0f);
		}
	}
	else {
		outputs[OUTL_OUTPUT].setVoltage(0.0f);
		outputs[OUTR_OUTPUT].setVoltage(0.0f);
	}

	outputs[EOC_OUTPUT].setVoltage(eocPulse.process(1 / args.sampleRate) ? 10.0f : 0.0f);
}

struct BidooTransientsBlueTrimpot : BidooBlueTrimpot {
	CANARD *module;

	void onButton(const event::Button &e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
			module->calcTransients();
		}
		BidooBlueTrimpot::onButton(e);
	}
};

struct CANARDDisplay : OpaqueWidget {
	CANARD *module;
	const float width = 175.0f;
	const float height = 50.0f;
	float zoomWidth = 175.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	CANARDDisplay() {

	}

	void onButton(const event::Button &e) override {
		if (module->slices.size()>0) {
			refX = e.pos.x;
			refIdx = ((e.pos.x - zoomLeftAnchor)/zoomWidth)*(float)module->totalSampleCount;
			module->addSliceMarker = refIdx;
			auto lower = std::lower_bound(module->slices.begin(), module->slices.end(), refIdx);
			module->selected = distance(module->slices.begin(),lower-1);
			module->deleteSliceMarker = *(lower-1);
		}
		if (e.button == 0)
			OpaqueWidget::onButton(e);
		else {
			if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
				ModuleWidget *pWidget = dynamic_cast<ModuleWidget*>(this->parent);
				pWidget->createContextMenu();
				e.consume(this);
			}
		}
	}

	void onDragStart(const event::DragStart &e) override {
		APP->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		float zoom = 1.0f;
		if (e.mouseDelta.y > 0.0f) {
			zoom = 1.0f/(((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f);
		}
		else if (e.mouseDelta.y < 0.0f) {
			zoom = ((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f;
		}
		zoomWidth = clamp(zoomWidth*zoom,width,zoomWidth*((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT) ? 2.0f : 1.1f));
		zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor)*zoom + e.mouseDelta.x, width - zoomWidth,0.0f);
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const event::DragEnd &e) override {
		APP->window->cursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module && (module->playBuffer.size()>0)) {
				if (module->try_lock()) {
					std::vector<int> s(module->slices);
					size_t nbSample = module->totalSampleCount;

					nvgScissor(args.vg, 0, 0, width, 2*height+10);

					// Draw play line
					if (!module->loading) {
						nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
						{
							nvgBeginPath(args.vg);
							nvgStrokeWidth(args.vg, 2);
							if (nbSample>0) {
								nvgMoveTo(args.vg, module->samplePos * zoomWidth / nbSample + zoomLeftAnchor, 0);
								nvgLineTo(args.vg, module->samplePos * zoomWidth / nbSample + zoomLeftAnchor, 2*height+10);
							}
							else {
								nvgMoveTo(args.vg, 0, 0);
								nvgLineTo(args.vg, 0, 2*height+10);
							}
							nvgClosePath(args.vg);
						}
						nvgStroke(args.vg);
					}

					// Draw ref line
					nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
					nvgStrokeWidth(args.vg, 1);
					{
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, 0, height/2);
						nvgLineTo(args.vg, width, height/2);
						nvgClosePath(args.vg);
					}
					nvgStroke(args.vg);

					nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
					nvgStrokeWidth(args.vg, 1);
					{
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, 0, 3*height*0.5f+10);
						nvgLineTo(args.vg, width, 3*height*0.5f+10);
						nvgClosePath(args.vg);
					}
					nvgStroke(args.vg);

					if ((!module->loading) && (nbSample>0)) {

						// Draw loop
						nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 60));
						nvgStrokeWidth(args.vg, 1);
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, (module->sampleStart + module->fadeLenght) * zoomWidth / nbSample + zoomLeftAnchor, 0);
						nvgLineTo(args.vg, module->sampleStart * zoomWidth / nbSample + zoomLeftAnchor, 2*height+10);
						nvgLineTo(args.vg, (module->sampleStart + module->loopLength) * zoomWidth / nbSample + zoomLeftAnchor, 2*height+10);
						nvgLineTo(args.vg, (module->sampleStart + module->loopLength - module->fadeLenght) * zoomWidth / nbSample + zoomLeftAnchor, 0);
						nvgLineTo(args.vg, (module->sampleStart + module->fadeLenght) * zoomWidth / nbSample + zoomLeftAnchor, 0);
						nvgClosePath(args.vg);
						nvgFill(args.vg);

						//draw selected
						if ((module->selected >= 0) && ((size_t)module->selected < s.size()) && (floor(module->params[CANARD::MODE_PARAM].getValue()) == 1)) {
							nvgStrokeColor(args.vg, RED_BIDOO);
								nvgBeginPath(args.vg);
							nvgStrokeWidth(args.vg, 4);
							nvgMoveTo(args.vg, (s[module->selected] * zoomWidth / nbSample) + zoomLeftAnchor , 2*height+9);
							if ((size_t)module->selected < (s.size()-1))
								nvgLineTo(args.vg, (s[module->selected+1] * zoomWidth / nbSample) + zoomLeftAnchor , 2*height+9);
							else
								nvgLineTo(args.vg, zoomWidth + zoomLeftAnchor, 2*height+9);
							nvgClosePath(args.vg);
							nvgStroke(args.vg);
						}

						// Draw waveform

						if (nbSample>0) {
							nvgStrokeColor(args.vg, PINK_BIDOO);
							nvgSave(args.vg);
							Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
							float invNbSample = 1.0f / nbSample;
							size_t inc = std::max(nbSample/zoomWidth/4,1.f);
							nvgBeginPath(args.vg);
							for (size_t i = 0; i < nbSample; i+=inc) {
								float x, y;
								x = (float)i * invNbSample ;
								y = (-1.f)*module->playBuffer[i].samples[0] * 0.5f + 0.5f;
								Vec p;
								p.x = b.pos.x + b.size.x * x;
								p.y = b.pos.y + b.size.y * (1.0f - y);
								if (i == 0) {
									nvgMoveTo(args.vg, p.x, p.y);
								}
								else {
									nvgLineTo(args.vg, p.x, p.y);
								}
							}

							nvgLineCap(args.vg, NVG_MITER);
							nvgStrokeWidth(args.vg, 1);
							nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
							nvgStroke(args.vg);

							b = Rect(Vec(zoomLeftAnchor, height+10), Vec(zoomWidth, height));
							nvgBeginPath(args.vg);
							for (size_t i = 0; i < nbSample; i+=inc) {
								float x, y;
								x = (float)i * invNbSample;
								y = (-1.f)*module->playBuffer[i].samples[1] * 0.5f + 0.5f;
								Vec p;
								p.x = b.pos.x + b.size.x * x;
								p.y = b.pos.y + b.size.y * (1.0f - y);
								if (i == 0)
									nvgMoveTo(args.vg, p.x, p.y);
								else {
									nvgLineTo(args.vg, p.x, p.y);
								}
							}
							nvgLineCap(args.vg, NVG_MITER);
							nvgStrokeWidth(args.vg, 1);
							nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
							nvgStroke(args.vg);
						}

						//draw slices

						if (floor(module->params[CANARD::MODE_PARAM].getValue()) == 1) {
							for (size_t i = 0; i < s.size(); i++) {
								if (s[i] != module->deleteSliceMarker) {
									nvgStrokeColor(args.vg, YELLOW_BIDOO);
								}
								else {
									nvgStrokeColor(args.vg, RED_BIDOO);
								}
								nvgStrokeWidth(args.vg, 1);
								{
									nvgBeginPath(args.vg);
									nvgMoveTo(args.vg, s[i] * zoomWidth / nbSample + zoomLeftAnchor , 0);
									nvgLineTo(args.vg, s[i] * zoomWidth / nbSample + zoomLeftAnchor , 2*height+10);
									nvgClosePath(args.vg);
								}
								nvgStroke(args.vg);
							}
						}

					}
					nvgResetScissor(args.vg);
					nvgRestore(args.vg);

					module->unlock();
				}
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct CANARDWidget : BidooWidget {

	CANARDWidget(CANARD *module) {
		printf("CANARDWidget: module is %p\n", module);
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/CANARD.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			CANARDDisplay *display = new CANARDDisplay();
			display->module = module;
			display->box.pos = Vec(10, 35);
			display->box.size = Vec(175, 110);
			addChild(display);
		}

		static const float portX0[5] = {16, 53, 90, 126, 161};

		addChild(createLight<SmallLight<RedLight>>(Vec(portX0[0]-10, 167), module, CANARD::REC_LIGHT));
		addParam(createParam<BlueCKD6>(Vec(portX0[0]-6, 170), module, CANARD::RECORD_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(portX0[2]-7, 170), module, CANARD::SAMPLE_START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[3]-7, 170), module, CANARD::LOOP_LENGTH_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0[4]-6, 170), module, CANARD::READ_MODE_PARAM));

		addInput(createInput<PJ301MPort>(Vec(portX0[0]-4.5f, 202), module, CANARD::RECORD_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4.5f, 202), module, CANARD::TRIG_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4.5f, 172), module, CANARD::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2]-5.0f, 202), module, CANARD::SAMPLE_START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3]-5.0f, 202), module, CANARD::LOOP_LENGTH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[4]-4.5f, 202), module, CANARD::READ_MODE_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(portX0[0]-7, 245), module, CANARD::SPEED_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-7, 245), module, CANARD::FADE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[2]-7, 245), module, CANARD::SLICE_PARAM));
		addParam(createParam<BlueCKD6>(Vec(portX0[3]-6, 245), module, CANARD::CLEAR_PARAM));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[4]-4.5f, 247), module, CANARD::EOC_OUTPUT));

		addInput(createInput<PJ301MPort>(Vec(portX0[0]-4.5f, 277), module, CANARD::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4.5f, 277), module, CANARD::FADE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2]-4.5f, 277), module, CANARD::SLICE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3]-5.0f, 277), module, CANARD::CLEAR_INPUT));

		BidooTransientsBlueTrimpot *trim = createParam<BidooTransientsBlueTrimpot>(Vec(portX0[4]-1, 280), module, CANARD::THRESHOLD_PARAM);
		trim->module=module;
		addParam(trim);

		addParam(createParam<CKSS>(Vec(89, 325), module, CANARD::MODE_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(8, 340), module, CANARD::INL_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8+22, 340), module, CANARD::INR_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(150, 340), module, CANARD::OUTL_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(150+22, 340), module, CANARD::OUTR_OUTPUT));
	}

	struct CANARDDeleteSlice : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->deleteFlag = true;
		}
	};

	struct CANARDDeleteSliceMarker : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->deleteSliceMarkerFlag = true;
		}
	};

	struct CANARDAddSliceMarker : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->addSliceMarkerFlag = true;
		}
	};

	struct CANARDTransientDetect : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			APP->engine->yieldWorkers();
			module->calcTransients();
		}
	};

	struct CANARDLoadSample : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
			#if defined(METAMODULE)
			async_osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL, [this](char *path) {
				if (path) {
					// printf("Load sample: module is %p\n", module);
					module->lastPath = path;
					module->loading=true;
					free(path);
				}
			});
			#else
			char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
			if (path) {
				module->lastPath = path;
				module->loading=true;
				free(path);
			}
			#endif
		}
	};


	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		CANARD *module = dynamic_cast<CANARD*>(this->module);
		module->lastPath = e.paths[0];
		module->loading=true;
	}

	struct CANARDSaveSample : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
			std::string fileName = module->waveFileName.empty() ? "temp.wav" : module->waveFileName;
			#if defined(METAMODULE)
			async_osdialog_file(OSDIALOG_SAVE, dir.c_str(), fileName.c_str(), NULL, [this](char *path) {
				if (path) {
					module->lastPath = path;
					if (!module->save) module->save = true;
					free(path);
				}
			});
			#else
			char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), fileName.c_str(), NULL);			
			if (path) {
				module->lastPath = path;
				if (!module->save) module->save = true;
				free(path);
			}
			#endif
		}
	};


	void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		CANARD *module = dynamic_cast<CANARD*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<CANARDDeleteSlice>(&MenuItem::text, "Delete slice", &CANARDDeleteSlice::module, module));
		menu->addChild(construct<CANARDDeleteSliceMarker>(&MenuItem::text, "Delete slice marker", &CANARDDeleteSliceMarker::module, module));
		menu->addChild(construct<CANARDAddSliceMarker>(&MenuItem::text, "Add slice marker", &CANARDAddSliceMarker::module, module));
		menu->addChild(construct<CANARDTransientDetect>(&MenuItem::text, "Detect transients", &CANARDTransientDetect::module, module));
		menu->addChild(construct<CANARDLoadSample>(&MenuItem::text, "Load sample", &CANARDLoadSample::module, module));
		menu->addChild(construct<CANARDSaveSample>(&MenuItem::text, "Save sample", &CANARDSaveSample::module, module));
	}
};

Model *modelCANARD = createModel<CANARD, CANARDWidget>("cANARd");
