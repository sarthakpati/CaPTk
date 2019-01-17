/**
\file  TrainingModule.h

\brief The header file containing the TrainingModule class, used to build machine learning models
Author: Saima Rathore
Library Dependecies: ITK 4.7+ <br>

http://www.med.upenn.edu/sbia/software/ <br>
software@cbica.upenn.edu

Copyright (c) 2016 University of Pennsylvania. All rights reserved. <br>
See COPYING file or https://www.med.upenn.edu/sbia/software-agreement.html

*/
#pragma once

//#include "cbicaUtilities.h"
//#include "FeatureReductionClass.h"
//#include "CAPTk.h"
#include "itkExtractImageFilter.h"
#include "itkCSVArray2DFileReader.h"
#include "FeatureScalingClass.h"
#include "CapTkDefines.h"
#include "cbicaLogging.h"


#ifdef APP_BASE_CAPTK_H
#include "ApplicationBase.h"
#endif

typedef itk::Image< float, 3 > ImageType;
typedef std::tuple<VectorDouble, VectorDouble, VariableSizeMatrixType, VectorDouble, VectorDouble, VariableSizeMatrixType, VectorDouble> FoldTupleType;
typedef std::map<int, FoldTupleType> MapType;

typedef itk::CSVArray2DFileReader<double> CSVFileReaderType;
typedef vnl_matrix<double> MatrixType;

class TrainingModule
#ifdef APP_BASE_CAPTK_H
  : public ApplicationBase
#endif
{
public:

  cbica::Logging logger;

  //! Default constructor
  TrainingModule() {};
  ~TrainingModule() {};

  bool Run(const std::string inputFeaturesFile, const std::string inputLabelsFile, const std::string outputdirectory,const int classifierType, const int foldtype, const int conftype);

  std::string mEighteenTrainedFile, mSixTrainedFile;

  VectorDouble CalculatePerformanceMeasures(VariableLengthVectorType predictedLabels, std::vector<double> GivenLabels);

  bool CheckPerformanceStatus(double ist, double second, double third, double fourth, double fifth, double sixth, double seventh, double eighth, double ninth, double tenth);
  VectorDouble CalculatePerformanceMeasures(VectorDouble predictedLabels, VectorDouble GivenLabels);

  VectorDouble CrossValidation(const VariableSizeMatrixType inputFeatures, const VariableLengthVectorType inputLabels, const std::string outputfolder,
    const int classifiertype, const int foldtype);

  VectorDouble InternalCrossValidation(VariableSizeMatrixType inputFeatures, std::vector<double> inputLabels, double cValue, double gValue,int kerneltype);


  VectorDouble SplitTrainTest(const VariableSizeMatrixType inputFeatures, const VariableLengthVectorType inputLabels, const std::string outputfolder, const int classifiertype, const int training_size);

  VectorDouble trainOpenCVSVM(const VariableSizeMatrixType &trainingDataAndLabels, const std::string &outputModelName, bool considerWeights, int ApplicationCallingSVM, double bestc, double bestg);

  VectorDouble testOpenCVSVM(const VariableSizeMatrixType &testingData, const std::string &inputModelName);

  VectorDouble CombineEstimates(const VariableLengthVectorType &estimates1, const VariableLengthVectorType &estimates2);

  VectorDouble EffectSizeFeatureSelection(const VariableSizeMatrixType training_features, std::vector<double> target);

  std::string CheckDataQuality(const VariableSizeMatrixType & FeaturesOfAllSubjects, const VariableLengthVectorType & LabelsOfAllSubjects);

  VectorDouble InternalCrossValidationSplitTrainTest(VariableSizeMatrixType inputFeatures, std::vector<double> inputLabels, double cValue, double gValue, int kerneltype, int counter, std::string outputfolder);
  template <typename T>
  std::vector<size_t> sort_indexes(const std::vector<T> &v);

private:

};