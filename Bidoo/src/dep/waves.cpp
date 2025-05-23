#include "waves.hpp"
#include "AudioFile/AudioFile.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav/dr_wav.h"
#include <dsp/resampler.hpp>

namespace waves {

  std::vector<rack::dsp::Frame<1>> getMonoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount) {
    waveFileName = rack::system::getFilename(path);
    waveExtension = rack::system::getExtension(waveFileName);
    std::vector<rack::dsp::Frame<1>> result;
    if (rack::string::uppercase(waveExtension) == ".WAV") {
      unsigned int c;
      unsigned int sr;
      drwav_uint64 sc;
      float* pSampleData;
      pSampleData = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &c, &sr, &sc, NULL);
      if (pSampleData != NULL)  {
        sampleChannels = c;
        sampleRate = sr;
        for (long long unsigned int i=0; i < sc; i = i + c) {
          rack::dsp::Frame<1> frame;
          if (sampleChannels == 2) {
            frame.samples[0] = (pSampleData[i] + pSampleData[i+1])/2.0f;
          }
          else {
            frame.samples[0] = pSampleData[i];
          }
          result.push_back(frame);
        }
        sampleCount = sc/c;
        drwav_free(pSampleData, NULL);
      }
    }
    else if (rack::string::uppercase(waveExtension) == ".AIFF") {
      AudioFile<float> audioFile;
      if (audioFile.load (path.c_str()))  {
        sampleChannels = audioFile.getNumChannels();
        sampleRate = audioFile.getSampleRate();
        sampleCount = audioFile.getNumSamplesPerChannel();
        for (int i=0; i < sampleCount; i++) {
          rack::dsp::Frame<1> frame;
          if (sampleChannels == 2) {
            frame.samples[0] = (audioFile.samples[0][i] + audioFile.samples[1][i])/2.0f;
          }
          else {
            frame.samples[0] = audioFile.samples[0][i];
          }
          result.push_back(frame);
        }
      }
    }

    if ((sampleRate != currentSampleRate) && (sampleCount>0)) {
      rack::dsp::SampleRateConverter<1> conv;
      conv.setRates(sampleRate, currentSampleRate);
      conv.setQuality(SPEEX_RESAMPLER_QUALITY_DESKTOP);
      int outCount = sampleCount;
      std::vector<rack::dsp::Frame<1>> subResult;
      for (int i=0;i<sampleCount;i++) {
        rack::dsp::Frame<1> frame;
        frame.samples[0]=0.0f;
        subResult.push_back(frame);
      }
      conv.process(&result[0], &sampleCount, &subResult[0], &outCount);
      sampleCount = outCount;
      return subResult;
    }

    return result;
  }

  std::vector<rack::dsp::Frame<2>> getStereoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount) {
    waveFileName = rack::system::getFilename(path);
    waveExtension = rack::system::getExtension(waveFileName);
    std::vector<rack::dsp::Frame<2>> result;

    // Convert extension to uppercase for comparison
    std::string upperExt = rack::string::uppercase(waveExtension);
    
    // Check if extension is supported
    if (upperExt != ".WAV" && upperExt != ".AIFF") {
        // Return empty buffer for unsupported format
        sampleChannels = 0;
        sampleRate = 0;
        sampleCount = 0;
        return result;
    }

    if (upperExt == ".WAV") {
      unsigned int c;
      unsigned int sr;
      drwav_uint64 sc;
      float* pSampleData;
      pSampleData = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &c, &sr, &sc, NULL);
      if (pSampleData != NULL)  {
        sampleChannels = c;
        sampleRate = sr;
        sc = 2 * sc;
        for (long long unsigned int i=0; i < sc; i = i + c) {
          rack::dsp::Frame<2> frame;
          frame.samples[0] = pSampleData[i];
  				if (sampleChannels == 2)
  					frame.samples[1] = (float)pSampleData[i+1];
  				else
  					frame.samples[1] = (float)pSampleData[i];
          result.push_back(frame);
        }
        sampleCount = sc/c;
        drwav_free(pSampleData, NULL);
      }
    }
    else if (upperExt == ".AIFF") {
      AudioFile<float> audioFile;
      if (audioFile.load (path.c_str()))  {
        sampleChannels = audioFile.getNumChannels();
        sampleRate = audioFile.getSampleRate();
        sampleCount = audioFile.getNumSamplesPerChannel();
        for (int i=0; i < sampleCount; i++) {
          rack::dsp::Frame<2> frame;
          frame.samples[0] = audioFile.samples[0][i];
  				if (sampleChannels == 2)
  					frame.samples[1] = audioFile.samples[1][i];
  				else
  					frame.samples[1] = audioFile.samples[0][i];
          result.push_back(frame);
        }
      }
    }

    if ((sampleRate != currentSampleRate) && (sampleCount>0)) {
      rack::dsp::SampleRateConverter<2> conv;
      conv.setRates(sampleRate, currentSampleRate);
      conv.setQuality(SPEEX_RESAMPLER_QUALITY_DESKTOP);
      int outCount = 16*sampleCount;
      std::vector<rack::dsp::Frame<2>> subResult;
      for (int i=0;i<outCount;i++) {
        rack::dsp::Frame<2> frame;
        frame.samples[0]=0.0f;
        frame.samples[1]=0.0f;
        subResult.push_back(frame);
      }
      conv.process(&result[0], &sampleCount, &subResult[0], &outCount);
      sampleCount = outCount;
      return subResult;
    }

    return result;
  }

  void saveWave(std::vector<rack::dsp::Frame<2>> &sample, int sampleRate, std::string path) {
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 2;
    format.sampleRate = sampleRate;
    format.bitsPerSample = 32;

    int *pSamples = (int*)calloc(2*sample.size(),sizeof(int));
    memset(pSamples, 0, 2*sample.size()*sizeof(int));
    for (unsigned int i = 0; i < sample.size(); i++) {
    	*(pSamples+2*i)= floor(sample[i].samples[0]*1990000000);
    	*(pSamples+2*i+1)= floor(sample[i].samples[1]*1990000000);
    }

    drwav wav;
    drwav_init_file_write(&wav, path.c_str(), &format, NULL);
    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, 2*sample.size(), pSamples);
    drwav_uninit(&wav);

    free(pSamples);
  }

}
