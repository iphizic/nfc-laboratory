/*

  Copyright (c) 2021 Jose Vicente Campos Martinez - <josevcm@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include <cmath>
#include <functional>

#include <rt/Logger.h>

#include <nfc/Nfc.h>
#include <nfc/NfcDecoder.h>

#include <NfcTech.h>

#include <tech/NfcA.h>
#include <tech/NfcB.h>
#include <tech/NfcF.h>
#include <tech/NfcV.h>

//#define ENABLE_NFC_A_DECODER
#define ENABLE_NFC_B_DECODER
//#define ENABLE_NFC_F_DECODER
//#define ENABLE_NFC_V_DECODER

namespace nfc {

struct NfcDecoder::Impl
{
   // frame handler
   typedef std::function<bool(nfc::NfcFrame &frame)> FrameHandler;

   rt::Logger log {"NfcDecoder"};

   // NFC-A Decoder
   struct NfcA nfca;

   // NFC-B Decoder
   struct NfcB nfcb;

   // NFC-F Decoder
   struct NfcF nfcf;

   // NFC-V Decoder
   struct NfcV nfcv;

   // global decoder status
   struct DecoderStatus decoder;

   Impl();

   inline void configure(long sampleRate);

   inline std::list<NfcFrame> nextFrames(sdr::SignalBuffer &samples);

   inline void detectCarrier(std::list<NfcFrame> &frames);
};

NfcDecoder::NfcDecoder() : impl(std::make_shared<Impl>())
{
}

std::list<NfcFrame> NfcDecoder::nextFrames(sdr::SignalBuffer samples)
{
   return impl->nextFrames(samples);
}

void NfcDecoder::setSampleRate(long sampleRate)
{
   impl->configure(sampleRate);
}

void NfcDecoder::setPowerLevelThreshold(float value)
{
   impl->decoder.powerLevelThreshold = value;
}

void NfcDecoder::setModulationThresholdNfcA(float min)
{
   impl->nfca.setModulationThreshold(min);
}

void NfcDecoder::setModulationThresholdNfcB(float min, float max)
{
   impl->nfcb.setModulationThreshold(min, max);
}

void NfcDecoder::setModulationThresholdNfcF(float min, float max)
{
   impl->nfcf.setModulationThreshold(min, max);
}

void NfcDecoder::setModulationThresholdNfcV(float min)
{
   impl->nfcv.setModulationThreshold(min);
}

float NfcDecoder::powerLevelThreshold() const
{
   return impl->decoder.powerLevelThreshold;
}

float NfcDecoder::signalStrength() const
{
   return impl->decoder.signalStatus.powerAverage;
}

NfcDecoder::Impl::Impl() : nfca(&decoder), nfcb(&decoder), nfcf(&decoder), nfcv(&decoder)
{
}

/**
 * Configure samplerate
 */
void NfcDecoder::Impl::configure(long newSampleRate)
{
   // clear signal parameters
   decoder.signalParams = {0,};

   // clear signal processing status
   decoder.signalStatus = {0,};

   // set decoder samplerate
   decoder.sampleRate = newSampleRate;

   // clear signal master clock
   decoder.signalClock = 0;

   // configure only if samplerate > 0
   if (decoder.sampleRate > 0)
   {
      // calculate sample time unit, (equivalent to 1/fc in ISO/IEC 14443-3 specifications)
      decoder.signalParams.sampleTimeUnit = double(decoder.sampleRate) / double(NFC_FC);

#ifdef ENABLE_NFC_A_DECODER
      // configure NFC-A decoder
      nfca.configure(newSampleRate);
#endif

#ifdef ENABLE_NFC_B_DECODER
      // configure NFC-B decoder
      nfcb.configure(newSampleRate);
#endif

#ifdef ENABLE_NFC_F_DECODER
      // configure NFC-F decoder
      nfcf.configure(newSampleRate);
#endif

#ifdef ENABLE_NFC_V_DECODER
      // configure NFC-V decoder
      nfcv.configure(newSampleRate);
#endif

#ifdef DEBUG_SIGNAL
      log.warn("SIGNAL DEBUGGER ENABLED!, highly affected performance!");
      decoder.debug = std::make_shared<SignalDebug>(DEBUG_CHANNELS, decoder.sampleRate);
#endif
   }

   // starts without bitrate
   decoder.bitrate = nullptr;

   // starts without modulation
   decoder.modulation = nullptr;
}

/**
 * Extract next frames
 */
std::list<NfcFrame> NfcDecoder::Impl::nextFrames(sdr::SignalBuffer &samples)
{
   // detected frames
   std::list<NfcFrame> frames;

   // only process valid sample buffer
   if (samples.isValid())
   {
      // re-configure decoder parameters on sample rate changes
      if (decoder.sampleRate != samples.sampleRate())
      {
         configure(samples.sampleRate());
      }

#ifdef DEBUG_SIGNAL
      decoder.debug->begin(samples.elements());
#endif

      do
      {
         if (!decoder.modulation)
         {
            // clear bitrate
            decoder.bitrate = nullptr;

            // NFC modulation detector for NFC-A / B / F / V
            while (decoder.nextSample(samples))
            {
               // carrier detector
               detectCarrier(frames);

#ifdef ENABLE_NFC_A_DECODER
               if (nfca.detect())
                  break;
#endif

#ifdef ENABLE_NFC_B_DECODER
               if (nfcb.detect())
                  break;
#endif

#ifdef ENABLE_NFC_F_DECODER
               if (nfcf.detect())
                  break;
#endif

#ifdef ENABLE_NFC_V_DECODER
               if (nfcv.detect())
                  break;
#endif
            }
         }

         if (decoder.bitrate)
         {
            switch (decoder.bitrate->techType)
            {
#ifdef ENABLE_NFC_A_DECODER
               case TechType::NfcA:
                  nfca.decode(samples, frames);
                  break;
#endif

#ifdef ENABLE_NFC_B_DECODER
               case TechType::NfcB:
                  nfcb.decode(samples, frames);
                  break;
#endif

#ifdef ENABLE_NFC_F_DECODER
                  case TechType::NfcF:
                     nfcf.decode(samples, frames);
                     break;
#endif

#ifdef ENABLE_NFC_V_DECODER
                  case TechType::NfcV:
                     nfcv.decode(samples, frames);
                     break;
#endif
            }
         }

      } while (!samples.isEmpty());

#ifdef DEBUG_SIGNAL
      decoder.debug->write();
#endif
   }

      // if sample buffer is not valid only process remain carrier detector
   else
   {
      if (decoder.signalStatus.carrierOff)
      {
         NfcFrame silence = NfcFrame(TechType::None, FrameType::NoCarrier);

         silence.setFramePhase(FramePhase::CarrierFrame);
         silence.setSampleStart(decoder.signalStatus.carrierOff);
         silence.setSampleEnd(decoder.signalClock);
         silence.setTimeStart(double(decoder.signalStatus.carrierOff) / double(decoder.sampleRate));
         silence.setTimeEnd(double(decoder.signalClock) / double(decoder.sampleRate));

         frames.push_back(silence);
      }

      else if (decoder.signalStatus.carrierOn)
      {
         NfcFrame carrier = NfcFrame(TechType::None, FrameType::EmptyFrame);

         carrier.setFramePhase(FramePhase::CarrierFrame);
         carrier.setSampleStart(decoder.signalStatus.carrierOn);
         carrier.setSampleEnd(decoder.signalClock);
         carrier.setTimeStart(double(decoder.signalStatus.carrierOn) / double(decoder.sampleRate));
         carrier.setTimeEnd(double(decoder.signalClock) / double(decoder.sampleRate));

         frames.push_back(carrier);
      }
   }

   // return frame list
   return frames;
}

void NfcDecoder::Impl::detectCarrier(std::list<NfcFrame> &frames)
{
   /*
    * carrier presence detector
    */
   float edge = std::fabs(decoder.signalStatus.signalAverage - decoder.signalStatus.powerAverage);

   // positive edge
   if (decoder.signalStatus.signalAverage > edge && decoder.signalStatus.powerAverage > decoder.powerLevelThreshold)
   {
      if (!decoder.signalStatus.carrierOn)
      {
         decoder.signalStatus.carrierOn = decoder.signalClock;

         if (decoder.signalStatus.carrierOff)
         {
            NfcFrame silence = NfcFrame(TechType::None, FrameType::NoCarrier);

            silence.setFramePhase(FramePhase::CarrierFrame);
            silence.setSampleStart(decoder.signalStatus.carrierOff);
            silence.setSampleEnd(decoder.signalStatus.carrierOn);
            silence.setTimeStart(double(decoder.signalStatus.carrierOff) / double(decoder.sampleRate));
            silence.setTimeEnd(double(decoder.signalStatus.carrierOn) / double(decoder.sampleRate));

            frames.push_back(silence);
         }

         decoder.signalStatus.carrierOff = 0;
      }
   }

      // negative edge
   else if (decoder.signalStatus.signalAverage < edge || decoder.signalStatus.powerAverage < decoder.powerLevelThreshold)
   {
      if (!decoder.signalStatus.carrierOff)
      {
         decoder.signalStatus.carrierOff = decoder.signalClock;

         if (decoder.signalStatus.carrierOn)
         {
            NfcFrame carrier = NfcFrame(TechType::None, FrameType::EmptyFrame);

            carrier.setFramePhase(FramePhase::CarrierFrame);
            carrier.setSampleStart(decoder.signalStatus.carrierOn);
            carrier.setSampleEnd(decoder.signalStatus.carrierOff);
            carrier.setTimeStart(double(decoder.signalStatus.carrierOn) / double(decoder.sampleRate));
            carrier.setTimeEnd(double(decoder.signalStatus.carrierOff) / double(decoder.sampleRate));

            frames.push_back(carrier);
         }

         decoder.signalStatus.carrierOn = 0;
      }
   }
}

}