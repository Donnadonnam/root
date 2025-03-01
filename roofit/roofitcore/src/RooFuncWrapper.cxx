/*
 * Project: RooFit
 * Authors:
 *   Garima Singh, CERN 2022
 *
 * Copyright (c) 2022, CERN
 *
 * Redistribution and use in source and binary forms,
 * with or without modification, are permitted according to the terms
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)
 */

#include "RooFuncWrapper.h"

#include "TROOT.h"
#include "TSystem.h"
#include "RooAbsData.h"
#include "RooGlobalFunc.h"
#include "RooMsgService.h"
#include "RooRealVar.h"

RooFuncWrapper::RooFuncWrapper(const char *name, const char *title, std::string const &funcBody,
                               RooArgSet const &paramSet, RooArgSet const &ObsSet, const RooAbsData *data /*=nullptr*/)
   : RooAbsReal{name, title}, _params{"!params", "List of parameters", this}
{
   std::string funcName = name;
   std::string gradName = funcName + "_grad_0";
   std::string requestName = funcName + "_req";
   std::string wrapperName = funcName + "_derivativeWrapper";

   // Declare the function
   std::stringstream bodyWithSigStrm;
   bodyWithSigStrm << "double " << funcName << "(double* params, double* obs) {" << funcBody << "}";
   bool comp = gInterpreter->Declare(bodyWithSigStrm.str().c_str());
   if (!comp) {
      std::stringstream errorMsg;
      errorMsg << "Function " << funcName << " could not be compiled. See above for details.";
      coutE(InputArguments) << errorMsg.str() << std::endl;
      throw std::runtime_error(errorMsg.str().c_str());
   }
   _func = reinterpret_cast<Func>(gInterpreter->ProcessLine((funcName + ";").c_str()));

   // Calculate gradient
   gInterpreter->ProcessLine("#include <Math/CladDerivator.h>");
   // disable clang-format for making the following code unreadable.
   // clang-format off
      std::stringstream requestFuncStrm;
      requestFuncStrm << "#pragma clad ON\n"
                         "void " << requestName << "() {\n"
                         "  clad::gradient(" << funcName << ", \"params\");\n"
                         "}\n"
                         "#pragma clad OFF";
   // clang-format on
   comp = gInterpreter->Declare(requestFuncStrm.str().c_str());
   if (!comp) {
      std::stringstream errorMsg;
      errorMsg << "Function " << funcName << " could not be be differentiated. See above for details.";
      coutE(InputArguments) << errorMsg.str() << std::endl;
      throw std::runtime_error(errorMsg.str().c_str());
   }

   // Extract parameters
   for (auto *param : paramSet) {
      if (!dynamic_cast<RooAbsReal *>(param)) {
         std::stringstream errorMsg;
         errorMsg << "In creation of function " << funcName
                  << " wrapper: input param expected to be of type RooAbsReal.";
         coutE(InputArguments) << errorMsg.str() << std::endl;
         throw std::runtime_error(errorMsg.str().c_str());
      }
      _params.add(*param);
   }
   _gradientVarBuffer.reserve(_params.size());

   // Extract observables
   if (!ObsSet.empty()) {
      auto dataSpans = data->getBatches(0, data->numEntries());
      _observables.reserve(_observables.size() * data->numEntries());
      for (auto *obs : static_range_cast<RooRealVar *>(ObsSet)) {
         RooSpan<const double> span{dataSpans.at(obs)};
         for (int i = 0; i < data->numEntries(); ++i) {
            _observables.push_back(span[i]);
         }
      }
   }

   // Build a wrapper over the derivative to hide clad specific types such as 'array_ref'.
   // disable clang-format for making the following code unreadable.
   // clang-format off
      std::stringstream dWrapperStrm;
      dWrapperStrm << "void " << wrapperName << "(double* params, double* obs, double* out) {\n"
                      "  clad::array_ref<double> cladOut(out, " << _params.size() << ");\n"
                      "  " << gradName << "(params, obs, cladOut);\n"
                      "}";
   // clang-format on
   gInterpreter->Declare(dWrapperStrm.str().c_str());
   _grad = reinterpret_cast<Grad>(gInterpreter->ProcessLine((wrapperName + ";").c_str()));
}

RooFuncWrapper::RooFuncWrapper(const RooFuncWrapper &other, const char *name /*=nullptr*/)
   : RooAbsReal(other, name),
     _params("!params", this, other._params),
     _func(other._func),
     _grad(other._grad),
     _gradientVarBuffer(other._gradientVarBuffer),
     _observables(other._observables)
{
}

void RooFuncWrapper::getGradient(double *out) const
{
   updateGradientVarBuffer();

   _grad(_gradientVarBuffer.data(), _observables.data(), out);
}

void RooFuncWrapper::updateGradientVarBuffer() const
{
   std::transform(_params.begin(), _params.end(), _gradientVarBuffer.begin(),
                  [](RooAbsArg *obj) { return static_cast<RooAbsReal *>(obj)->getVal(); });
}

double RooFuncWrapper::evaluate() const
{
   updateGradientVarBuffer();

   return _func(_gradientVarBuffer.data(), _observables.data());
}
