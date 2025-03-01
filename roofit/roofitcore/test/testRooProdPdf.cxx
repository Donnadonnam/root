// Tests for the RooProdPdf
// Authors: Stephan Hageboeck, CERN  02/2019
//          Jonas Rembser, CERN, June 2021

#include <RooAddPdf.h>
#include <RooArgList.h>
#include <RooArgSet.h>
#include <RooConstVar.h>
#include <RooDataSet.h>
#include <RooFitResult.h>
#include <RooGenericPdf.h>
#include <RooProdPdf.h>
#include <RooRealVar.h>
#include <RooWorkspace.h>
#include <RooHelpers.h>

#include <gtest/gtest.h>

// Backward compatibility for gtest version < 1.10.0
#ifndef INSTANTIATE_TEST_SUITE_P
#define INSTANTIATE_TEST_SUITE_P INSTANTIATE_TEST_CASE_P
#endif

#include <memory>
#include <sstream>
#include <string>

class TestProdPdf : public ::testing::TestWithParam<std::tuple<int, std::string>> {
private:
   void SetUp() override
   {
      RooHelpers::LocalChangeMsgLevel chmsglvl{RooFit::WARNING, 0u, RooFit::NumIntegration, true};

      datap.reset(prod.generate(x, 1000));
      a.setConstant(true);

      _optimize = std::get<0>(GetParam());
      _batchMode = std::get<1>(GetParam());
   }

protected:
   constexpr static double bTruth = -0.5;

   RooRealVar x{"x", "x", 2., 0., 5.};
   RooRealVar a{"a", "a", -0.2, -5., 0.};
   RooRealVar b{"b", "b", bTruth, -5., 0.};

   RooGenericPdf c1{"c1", "exp(x[0]*x[1])", {x, a}};
   RooGenericPdf c2{"c2", "exp(x[0]*x[1])", {x, b}};
   RooProdPdf prod{"mypdf", "mypdf", {c1, c2}};
   std::unique_ptr<RooDataSet> datap;

   int _optimize = 0;
   std::string _batchMode;
};

TEST_P(TestProdPdf, CachingOpt)
{
   RooHelpers::LocalChangeMsgLevel chmsglvl{RooFit::WARNING, 0u, RooFit::NumIntegration, true};

   using namespace RooFit;
   prod.fitTo(*datap, Optimize(_optimize), PrintLevel(-1), BatchMode(_batchMode));
   EXPECT_LT(std::abs(b.getVal() - bTruth), b.getError() * 2.5) // 2.5-sigma compatibility check
      << "b=" << b.getVal() << " +- " << b.getError() << " doesn't match truth value with O" << _optimize << ".";
}

INSTANTIATE_TEST_SUITE_P(RooProdPdf, TestProdPdf,
                         testing::Values(TestProdPdf::ParamType{0, "off"}, TestProdPdf::ParamType{0, "cpu"},
                                         TestProdPdf::ParamType{1, "off"}, TestProdPdf::ParamType{1, "cpu"},
                                         TestProdPdf::ParamType{2, "off"}, TestProdPdf::ParamType{2, "cpu"}),
                         [](testing::TestParamInfo<TestProdPdf::ParamType> const &paramInfo) {
                            std::stringstream ss;
                            ss << "opt" << std::get<0>(paramInfo.param) << std::get<1>(paramInfo.param);
                            return ss.str();
                         });

std::vector<std::vector<RooAbsArg *>> allPossibleSubset(RooAbsCollection const &arr)
{
   std::vector<std::vector<RooAbsArg *>> out;
   std::size_t n = arr.size();

   std::size_t count = std::pow(2, n);
   for (std::size_t i = 0; i < count; i++) {
      out.emplace_back();
      auto &back = out.back();
      for (std::size_t j = 0; j < n; j++) {
         if ((i & (1 << j)) != 0) {
            back.push_back(arr[j]);
         }
      }
   }
   return out;
}

// Hash the integral configuration for all possible normalization sets.
unsigned int hashRooProduct(RooProdPdf const &prod)
{
   RooArgSet params;
   prod.getParameters(nullptr, params);
   auto subsets = allPossibleSubset(params);

   std::stringstream ss;

   for (auto const &subset : subsets) {
      // this can't be on the stack, otherwise we will always get the same
      // address and therefore get wrong cache hits!
      auto nset = std::make_unique<RooArgSet>(subset.begin(), subset.end());
      prod.writeCacheToStream(ss, nset.get());
   }

   std::string s = ss.str();
   return TString::Hash(s.c_str(), s.size());
}

TEST(RooProdPdf, TestGetPartIntList)
{
   // This test checks if RooProdPdf::getPartIntList factorizes the integrals
   // as expected.
   // Instead of trying to construct tests for all possible cases by hand,
   // this test creates a product where the factors have different patters of
   // overlapping parameters. To make sure all possible cases are covered, we
   // are using all possible subsets of the parameters one after the other to
   // create the reference test result.

   RooRealVar x{"x", "x", 1., 0, 10};
   RooRealVar y{"y", "y", 1., 0, 10};
   RooRealVar z{"z", "z", 1., 0, 10};

   RooRealVar m1{"m1", "m1", 1., 0, 10};
   RooRealVar m2{"m2", "m2", 1., 0, 10};
   RooRealVar m3{"m3", "m3", 1., 0, 10};

   RooGenericPdf gauss1{"gauss1", "gauss1", "x+m1", {x, m1}};
   RooGenericPdf gauss2{"gauss2", "gauss2", "x+m2", {x, m2}};
   RooGenericPdf gauss3{"gauss3", "gauss3", "y+m3", {y, m3}};
   RooGenericPdf gauss4{"gauss4", "gauss4", "z+m1", {z, m1}};
   RooGenericPdf gauss5{"gauss5", "gauss5", "x+m1", {x, m1}};

   // Product of all the pdfs.
   RooProdPdf prod{"prod", "prod", RooArgList{gauss1, gauss2, gauss3, gauss4, gauss5}};

   // We hash the string serializations of caches for all possible
   // normalization sets and compare it to the expected hash.
   // This value must be updated if the convention for integral names in
   // RooProdPdf changes.
   EXPECT_EQ(hashRooProduct(prod), 2448666198);
}

TEST(RooProdPdf, TestDepsAreCond)
{
   RooHelpers::LocalChangeMsgLevel chmsglvl{RooFit::WARNING, 0u, RooFit::Minimization, true};

   using namespace RooFit;

   RooWorkspace ws;

   ws.factory("GaussModel::gm(x[0, 1], 0.0, xErr[0.0001, 0.1])");
   ws.factory("Decay::decayPdf(x, tau[0.4, 0, 1], gm, RooDecay::SingleSided)");
   ws.factory("Gamma::errPdf(xErr, 4.0, 0.005, 0.0)");

   RooRealVar &x = *ws.var("x");
   RooRealVar &xErr = *ws.var("xErr");
   RooRealVar &tau = *ws.var("tau");
   RooAbsPdf &decayPdf = *ws.pdf("decayPdf");
   RooAbsPdf &errPdf = *ws.pdf("errPdf");

   // What we want: decayPdf(x|xErr)*errPdf(xErr):
   RooProdPdf pdf1("pdf1", "", {errPdf}, Conditional(decayPdf, x, false));

   // Should be the same as pdf1:
   RooProdPdf pdf2("pdf2", "", {errPdf}, Conditional(decayPdf, xErr, true));

   std::unique_ptr<RooDataSet> data{pdf1.generate({x, xErr}, NumEvents(10000))};

   auto resetParameters = [&]() {
      tau.setVal(0.4);
      tau.setError(0.0);
   };

   using ResultPtr = std::unique_ptr<RooFitResult>;

   ResultPtr result1{pdf1.fitTo(*data, Save(), BatchMode("off"), PrintLevel(-1))};
   resetParameters();
   ResultPtr result2{pdf1.fitTo(*data, Save(), BatchMode("cpu"), PrintLevel(-1))};
   resetParameters();
   ResultPtr result3{pdf2.fitTo(*data, Save(), BatchMode("off"), PrintLevel(-1))};
   resetParameters();
   ResultPtr result4{pdf2.fitTo(*data, Save(), BatchMode("cpu"), PrintLevel(-1))};

   EXPECT_TRUE(result2->isIdentical(*result1)) << "batchmode fit is inconsistent!";
   EXPECT_TRUE(result3->isIdentical(*result1)) << "alternative model fit is inconsistent!";
   EXPECT_TRUE(result4->isIdentical(*result1)) << "alternative model batchmode fit is inconsistent!";
}

/// This test covers a potential problem with the custom normalization ranges
/// of PDFs: the value of a RooProdPdf should not depend on the normalization
/// ranges of its clients. TODO: this is still a problem! It needs to be fixed,
/// and then the test can be enabled and this note should be removed.
TEST(RooProdPdf, DISABLED_ChangeServerNormSetForProdPdfInAddPdf)
{
   // Define the model
   RooWorkspace ws;
   ws.factory("PROD::sig(Gaussian(x[-10, 10], 1.0, 1.0), Gaussian(y[-10, 10], 1.0, 1.0))");
   ws.factory("PROD::bkg(Polynomial(x, {}), Polynomial(y, {}))");
   ws.factory("SUM::model(f[0.5, 0, 1] * sig, bkg)");
   ws.factory("SUM::modelFixed(f * sig, bkg)");
   ws.factory("EXPR::modelRef('f * 3.0/2000. * x * x + (1 - f) / 20.', {x, f})");

   RooRealVar &x = *ws.var("x");
   RooRealVar &y = *ws.var("y");

   RooAddPdf &model = static_cast<RooAddPdf &>(*ws.pdf("model"));

   // Define signal and sideband regions
   x.setRange("SB1", -10, +10);
   y.setRange("SB1", -10, 0);

   x.setRange("SB2", -10, 0);
   y.setRange("SB2", 0, +10);

   x.setRange("SIG", 0, +10);
   y.setRange("SIG", 0, +10);

   x.setRange("FULL", -10, +10);
   y.setRange("FULL", -10, +10);

   // Try different normalization sets to check if there is a false chache hit
   // after changing the normalization range of the servers.
   RooArgSet normSet1{x, y};
   RooArgSet normSet2{x, y};

   // The fit range needs to be a multi-range to trigger the problem that is covered by this test
   const char *fitRange = "SB1,SB2";

   // Set the normalization range for the top-level model
   model.setNormRange(fitRange);

   const double val1 = model.getVal(normSet1);

   // Now set the normalization range for all other PDFs, which should not
   // change the value of the top-level PDF.
   for (auto *pdf : static_range_cast<RooAbsPdf *>(ws.allPdfs())) {
      pdf->setNormRange(fitRange);
   }

   const double val2 = model.getVal(normSet1);
   const double val3 = model.getVal(normSet2);

   EXPECT_FLOAT_EQ(val2, val1);
   EXPECT_FLOAT_EQ(val3, val1);
}
