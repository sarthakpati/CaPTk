#include "ZScoreNormalizer.h"
#include "P1P2Normalizer.h"
#include "cbicaUtilities.h"
#include "cbicaCmdParser.h"
#include "cbicaLogging.h"
#include "cbicaITKUtilities.h"
#include "cbicaITKImageInfo.h"
#include "cbicaITKSafeImageIO.h"
#include "CaPTkGUIUtils.h"

#include "itkStatisticsImageFilter.h"
#include "itkBinaryFillholeImageFilter.h"
#include "itkJoinSeriesImageFilter.h"
#include "itkExtractImageFilter.h"
#include "itkPermuteAxesImageFilter.h"

#ifdef _WIN32
#include <direct.h>
#endif

//#include "CAPTk.h"

std::string inputT1ce, inputT1, inputT2, inputFlair, inputMaskName, modelDirName, inputBVecName, outputDirectory, loggerFileIn;
float quantLower = 5, quantUpper = 95, cutOffLower = 3, cutOffUpper = 3;
bool maskProvided = false, debugMode = false;
int inferenceType = 0;

enum InferenceTypes
{
  TumorSegmentation,
  SkullStripping,
  MaxType
};

template< class TImageType >
typename TImageType::Pointer HoleFillForSingleAxis(typename TImageType::Pointer input, size_t axisToIterate)
{
  auto size = input->GetLargestPossibleRegion().GetSize();
  auto origin = input->GetOrigin();
  typename TImageType::IndexType regionIndex;
  typename TImageType::SizeType regionSize;

  regionSize = size;
  regionSize[axisToIterate] = 0;
  regionIndex.Fill(0);

  itk::FixedArray< unsigned int, TImageType::ImageDimension > order;
  order[0] = 0;
  order[1] = 0;
  order[axisToIterate] = 1;
  auto permuter = itk::PermuteAxesImageFilter< TImageType >::New();
  permuter->SetInput(input);
  permuter->SetOrder(order);
  permuter->Update();
  cbica::WriteImage< TImageType >(permuter->GetOutput(), 
    outputDirectory + "/segm_permuter" + std::to_string(axisToIterate) + ".nii.gz");

  using TImageType2D = itk::Image< typename TImageType::PixelType, 2 >;
  auto extractor = itk::ExtractImageFilter< TImageType, TImageType2D >::New();
  extractor->SetInput(input);
  extractor->SetDirectionCollapseToIdentity(); // This is required.

  //auto joiner = itk::JoinSeriesImageFilter< itk::Image<float,2>, itk::Image<float, 3> >::New();
  auto joiner = itk::JoinSeriesImageFilter< TImageType2D, TImageType >::New();
  joiner->SetOrigin(input->GetOrigin()[axisToIterate]);
  joiner->SetSpacing(input->GetSpacing()[axisToIterate]);

  for (size_t i = 0; i < size[axisToIterate]; i++)
  {
    regionIndex[axisToIterate] = i;
    typename TImageType::RegionType desiredRegion(regionIndex, regionSize);
    extractor->SetExtractionRegion(desiredRegion);
    extractor->Update();

    auto debugImage = extractor->GetOutput();

    auto holeFiller = itk::BinaryFillholeImageFilter< TImageType2D >::New();
    holeFiller->SetInput(extractor->GetOutput());
    holeFiller->SetForegroundValue(1);
    holeFiller->Update();

    joiner->SetInput(i, holeFiller->GetOutput());
  }

  joiner->Update();
  return joiner->GetOutput();
}

template< class TImageType >
typename TImageType::Pointer HoleFillOnThreeAxes(typename TImageType::Pointer input)
{
  auto holeFiller = itk::BinaryFillholeImageFilter< TImageType >::New();
  holeFiller->SetInput(input);
  holeFiller->SetForegroundValue(1);
  holeFiller->SetFullyConnected(true);
  return holeFiller->GetOutput();

  auto output = HoleFillForSingleAxis< TImageType >(input, 2);
  if (cbica::ImageSanityCheck<TImageType>(input, output))
  {
    output = HoleFillForSingleAxis< TImageType >(output, 1);
    if (cbica::ImageSanityCheck<TImageType>(input, output))
    {
      output = HoleFillForSingleAxis< TImageType >(output, 0);
      if (cbica::ImageSanityCheck<TImageType>(input, output))
      {
        return output;
      }
    }
  }
  
  std::cerr << "Something went wrong with hole filling, please check.\n";
  exit(EXIT_FAILURE);
}

template< class TImageType >
void algorithmRunner()
{
  if (!cbica::isFile(modelDirName + "/modelConfig.txt"))
  {
    std::cerr << "'modelConfig.txt' was not found in the directory, please check.\n";
    return;
  }
  if (!cbica::isFile(modelDirName + "/model.ckpt.index"))
  {
    std::cerr << "'model.ckpt' was not found in the directory, please check.\n";
    return;
  }
  if (cbica::isFile(modelDirName + "/VERSION.yaml"))
  {
    if (!cbica::IsCompatible(modelDirName + "/VERSION.yaml"))
    {
      std::cerr << "The version of model is incompatible with this version of CaPTk.\n";
      return;
    }
  }
  auto filesInDir = cbica::filesInDirectory(modelDirName);
  for (size_t i = 0; i < filesInDir.size(); i++)
  {
    if (filesInDir[i].find("model.ckpt.data") != std::string::npos) // find an appopriate checkpoint
    {
      break;
    }
  }

  auto t1cImg = cbica::ReadImage< TImageType >(inputT1ce);
  auto t1Img = cbica::ReadImage< TImageType >(inputT1);
  auto t2Img = cbica::ReadImage< TImageType >(inputT2);
  auto flImg = cbica::ReadImage< TImageType >(inputFlair);
  auto maskImage = cbica::CreateImage< TImageType >(t1cImg);

  if (maskProvided)
  {
    maskImage = cbica::ReadImage< TImageType >(inputMaskName);
  }

  // TBD: this requires cleanup
  if (modelDirName.find("tumor") != std::string::npos)
  {
    inferenceType = 0;
  }
  else if (modelDirName.find("skull") != std::string::npos)
  {
    inferenceType = 1;
  }

  // per-patient registration
  auto greedyExe = getApplicationPath("GreedyRegistration");
  if (!cbica::ImageSanityCheck< TImageType >(t1cImg, maskImage))
  {
    auto tempFile_input = outputDirectory + "/maskToT1gd_input.nii.gz";
    auto tempFile = outputDirectory + "/maskToT1gd.nii.gz";
    cbica::WriteImage< TImageType >(maskImage, tempFile_input);
    auto greedyCommand = greedyExe +
      " -i " + tempFile_input +
      " -f " + inputT1ce +
      " -t " + outputDirectory + "/tempMatrix.mat" +
      " -o " + tempFile + " -reg -trf -a -m MI -n 100x50x5"
      ;

    std::cout << "== Starting per-subject registration of Mask to T1-Ce using Greedy.\n";
    std::system(greedyCommand.c_str());
    maskImage = cbica::ReadImage< TImageType >(tempFile);
    std::cout << "== Done.\n";
  }
  if (!cbica::ImageSanityCheck< TImageType >(t1cImg, t1Img))
  {
    auto tempFile = outputDirectory + "/T1ToT1gd.nii.gz";
    auto greedyCommand = greedyExe +
      " -i " + inputT1 +
      " -f " + inputT1ce +
      " -t " + outputDirectory + "/tempMatrix.mat" +
      " -o " + tempFile + " -reg -trf -a -m MI -n 100x50x5"
      ;

    std::cout << "== Starting per-subject registration of T1 to T1-Ce using Greedy.\n";
    std::system(greedyCommand.c_str());
    t1Img = cbica::ReadImage< TImageType >(tempFile);
    std::cout << "== Done.\n";
  }
  if (!cbica::ImageSanityCheck< TImageType >(t1cImg, t2Img))
  {
    auto tempFile = outputDirectory + "/T2ToT1gd.nii.gz";
    auto greedyCommand = greedyExe +
      " -i " + inputT2 +
      " -f " + inputT1ce +
      " -t " + outputDirectory + "/tempMatrix.mat" +
      " -o " + tempFile + " -reg -trf -a -m MI -n 100x50x5"
      ;

    std::cout << "== Starting per-subject registration of T2 to T1-Ce using Greedy.\n";
    std::system(greedyCommand.c_str());
    t2Img = cbica::ReadImage< TImageType >(tempFile);
    std::cout << "== Done.\n";
  }
  if (!cbica::ImageSanityCheck< TImageType >(t1cImg, flImg))
  {
    auto tempFile = outputDirectory + "/FLToT1gd.nii.gz";
    auto greedyCommand = greedyExe +
      " -i " + inputFlair +
      " -f " + inputT1ce +
      " -t " + outputDirectory + "/tempMatrix.mat" +
      " -o " + tempFile + " -reg -trf -a -m MI -n 100x50x5"
      ;

    std::cout << "== Starting per-subject registration of T2-Flair to T1-Ce using Greedy.\n";
    std::system(greedyCommand.c_str());
    flImg = cbica::ReadImage< TImageType >(tempFile);
    std::cout << "== Done.\n";
  }

  if (inferenceType == TumorSegmentation)
  {
    std::cout << "=== Checking and rectifying (z-score) normalization status.\n";
    auto statsCalculator = itk::StatisticsImageFilter< TImageType >::New();
    statsCalculator->SetInput(t1cImg);
    statsCalculator->Update();
    if (statsCalculator->GetMean() != 0)
    {
      std::cout << "== Starting Normalization of T1CE image.\n";
      ZScoreNormalizer< TImageType > normalizer;
      normalizer.SetInputImage(t1cImg);
      normalizer.SetInputMask(maskImage);
      normalizer.SetCutoffs(cutOffLower, cutOffUpper);
      normalizer.SetQuantiles(quantLower, quantUpper);
      normalizer.Update();
      t1cImg = normalizer.GetOutput();
      std::cout << "== Done.\n";
    }

    statsCalculator->SetInput(t1Img);
    statsCalculator->Update();
    if (statsCalculator->GetMean() != 0)
    {
      std::cout << "== Starting Normalization of T1 image.\n";
      ZScoreNormalizer< TImageType > normalizer;
      normalizer.SetInputImage(t1Img);
      normalizer.SetInputMask(maskImage);
      normalizer.SetCutoffs(cutOffLower, cutOffUpper);
      normalizer.SetQuantiles(quantLower, quantUpper);
      normalizer.Update();
      t1Img = normalizer.GetOutput();
      std::cout << "== Done.\n";
    }

    statsCalculator->SetInput(t2Img);
    statsCalculator->Update();
    if (statsCalculator->GetMean() != 0)
    {
      std::cout << "== Starting Normalization of T2 image.\n";
      ZScoreNormalizer< TImageType > normalizer;
      normalizer.SetInputImage(t2Img);
      normalizer.SetInputMask(maskImage);
      normalizer.SetCutoffs(cutOffLower, cutOffUpper);
      normalizer.SetQuantiles(quantLower, quantUpper);
      normalizer.Update();
      t2Img = normalizer.GetOutput();
      std::cout << "== Done.\n";
    }

    statsCalculator->SetInput(flImg);
    statsCalculator->Update();
    if (statsCalculator->GetMean() != 0)
    {
      std::cout << "== Starting Normalization of T2-Flair image.\n";
      ZScoreNormalizer< TImageType > normalizer;
      normalizer.SetInputImage(flImg);
      normalizer.SetInputMask(maskImage);
      normalizer.SetCutoffs(cutOffLower, cutOffUpper);
      normalizer.SetQuantiles(quantLower, quantUpper);
      normalizer.Update();
      flImg = normalizer.GetOutput();
      std::cout << "== Done.\n";
    }
    std::cout << "=== Done.\n";
  }

  if (inferenceType == SkullStripping)
  {
    std::cout << "=== Starting P1P2Normalize.\n";

    P1P2Normalizer< TImageType > normalizer;
    normalizer.SetInputImage(t1cImg);
    t1cImg = normalizer.GetOutput();
    normalizer.SetInputImage(t1Img);
    t1Img = normalizer.GetOutput();
    normalizer.SetInputImage(t2Img);
    t2Img = normalizer.GetOutput();
    normalizer.SetInputImage(flImg);
    flImg = normalizer.GetOutput();
    std::cout << "=== Done.\n";
  }

  if (inferenceType <= SkullStripping)
  {
    std::cout << "=== Starting resampling of images to isotropic resolution.\n";
    t1cImg = cbica::ResampleImage< TImageType >(t1cImg); // default is linear resampling to isotropic resolution of 1.0
    t1Img = cbica::ResampleImage< TImageType >(t1Img); // default is linear resampling to isotropic resolution of 1.0
    flImg = cbica::ResampleImage< TImageType >(flImg); // default is linear resampling to isotropic resolution of 1.0
    t2Img = cbica::ResampleImage< TImageType >(t2Img); // default is linear resampling to isotropic resolution of 1.0
    maskImage = cbica::ResampleImage< TImageType >(maskImage, 1.0, "Nearest"); // default is linear resampling to isotropic resolution of 1.0
    std::cout << "=== Done.\n";
  }

  cbica::createDir(outputDirectory);
  std::cout << "Starting DeepMedic Segmentation.\n";

  std::string file_t1ceNorm = cbica::normPath(outputDirectory + "/t1ce_normalized.nii.gz"),
    file_t1Norm = cbica::normPath(outputDirectory + "/t1_normalized.nii.gz"),
    file_t2Norm = cbica::normPath(outputDirectory + "/t2_normalized.nii.gz"),
    file_flNorm = cbica::normPath(outputDirectory + "/fl_normalized.nii.gz");
  cbica::WriteImage< TImageType >(t1cImg, file_t1ceNorm);
  cbica::WriteImage< TImageType >(t1Img, file_t1Norm);
  cbica::WriteImage< TImageType >(t2Img, file_t2Norm);
  cbica::WriteImage< TImageType >(flImg, file_flNorm);

  auto dmExe = getApplicationPath("deepMedicRun");
  //std::string dmExe = "C:/Projects/CaPTk_myFork/src/applications/individualApps/deepmedic/deepMedicRun.exe";

#ifdef _WIN32
  SetCurrentDirectory(cbica::getFilenamePath(dmExe).c_str());
#endif

  auto fullCommand = dmExe +
    " -t1 " + file_t1Norm +
    " -t1c " + file_t1ceNorm +
    " -t2 " + file_t2Norm +
    " -fl " + file_flNorm +
    " -model " + cbica::normPath(modelDirName + "/modelConfig.txt") +
    " -load " + cbica::normPath(modelDirName + "/model.ckpt") +
    " -test " + cbica::normPath(getCaPTkDataDir() + "/deepMedic/configFiles/testApiConfig.txt") +
    " -o " + outputDirectory;

  std::cout << "Running the following command:\n" << fullCommand << "\n";

  if (std::system(fullCommand.c_str()) != 0)
  {
    std::cerr << "DeepMedic exited with code !=0.\n";
    exit(EXIT_FAILURE);
  }

  auto outputImageFile = outputDirectory + "/predictions/testApiSession/predictions/Segm.nii.gz";
  // do hole filling for skull stripping
  if (inferenceType == SkullStripping)
  {
    std::cout << "=== Performing hole-filling operation for skull stripping.\n";
    if (cbica::exists(outputImageFile))
    {
      auto outputImageWithHoles = cbica::ReadImage< TImageType >(outputImageFile);

      auto holeFiller = itk::BinaryFillholeImageFilter< TImageType >::New();
      holeFiller->SetInput(outputImageWithHoles);
      holeFiller->SetForegroundValue(1);
      holeFiller->SetFullyConnected(false);
      holeFiller->Update();

      cbica::WriteImage< TImageType >(holeFiller->GetOutput(), outputImageFile);
    }
    std::cout << "=== Done.\n";
  }
  else if (inferenceType == TumorSegmentation)
  {
    auto outputImageWithOldValues = cbica::ReadImage< TImageType >(outputImageFile);
    auto outputImageWithNewValues = cbica::ChangeImageValues< TImageType >(outputImageWithOldValues, "3", "4");

    cbica::WriteImage< TImageType >(outputImageWithNewValues, outputImageFile);
  }

  // registration of segmentation back to patient space
  {
    std::cout << "== Starting registration of output segmentation back to patient space.\n";
    auto t1cImg_original = cbica::ReadImage< TImageType >(inputT1ce);
    auto resampledMask = cbica::ResampleImage< TImageType >(cbica::ReadImage< TImageType >(outputImageFile), 
      t1cImg_original->GetSpacing(),
      t1cImg_original->GetLargestPossibleRegion().GetSize(), "nearest");
    cbica::WriteImage< TImageType >(
      resampledMask,
      outputImageFile
      );
    std::cout << "== Done.\n";
  }

  return;
}

int main(int argc, char **argv)
{
  cbica::CmdParser parser(argc, argv, "DeepMedic");

  parser.addRequiredParameter("t1c", "T1CE", cbica::Parameter::FILE, "", "The input T1CE or T1Gd image file.");
  parser.addRequiredParameter("t1", "T1", cbica::Parameter::FILE, "", "The input T1 image file.");
  parser.addRequiredParameter("fl", "FLAIR", cbica::Parameter::FILE, "", "The input T2-FLAIR image file.");
  parser.addRequiredParameter("t2", "FLAIR", cbica::Parameter::FILE, "", "The input T2 image file.");
  parser.addOptionalParameter("m", "mask", cbica::Parameter::FILE, "", "The Optional input mask file.", "This is needed for normalization only");
  parser.addOptionalParameter("md", "modelDir", cbica::Parameter::DIRECTORY, "", "The trained model to use", "Defaults to 'CaPTk_installDir/data/deepMedic/brainSegmentation'");
  parser.addRequiredParameter("o", "output", cbica::Parameter::DIRECTORY, "", "The output directory");

  parser.addOptionalParameter("ql", "quantLower", cbica::Parameter::FLOAT, "0-100", "The Lower Quantile range to remove", "This is needed for normalization only", "Default: 5");
  parser.addOptionalParameter("qu", "quantUpper", cbica::Parameter::FLOAT, "0-100", "The Upper Quantile range to remove", "This is needed for normalization only", "Default: 95");
  parser.addOptionalParameter("cl", "cutOffLower", cbica::Parameter::FLOAT, "0-10", "The Lower Cut-off (multiple of stdDev) to remove", "This is needed for normalization only", "Default: 3");
  parser.addOptionalParameter("cu", "cutOffUpper", cbica::Parameter::FLOAT, "0-10", "The Upper Cut-off (multiple of stdDev) to remove", "This is needed for normalization only", "Default: 3");
  parser.addOptionalParameter("L", "Logger", cbica::Parameter::STRING, "log file which user has write access to", "Full path to log file to store console outputs", "By default, only console output is generated");
  parser.addApplicationDescription("This is a Deep Learning based inference engine based on DeepMedic (see documentation for details)");
  parser.addExampleUsage("-t1 c:/t1.nii.gz -t1c c:/t1gc.nii.gz -t2 c:/t2.nii.gz -fl c:/fl.nii.gz -o c:/output -md c:/CaPTk_install/data/deepMedic/saved_models/skullStripping", "This does a skull stripping of the input structural data");
  parser.addExampleUsage("-t1 c:/t1.nii.gz -t1c c:/t1gc.nii.gz -t2 c:/t2.nii.gz -fl c:/fl.nii.gz -o c:/output -md c:/CaPTk_install/data/deepMedic/saved_models/brainTumorSegmentation", "This does a tumor segmentation of the input structural data");

  // parameters to get from the command line
  cbica::Logging logger;

  if (parser.isPresent("L"))
  {
    parser.getParameterValue("L", loggerFileIn);
    logger.UseNewFile(loggerFileIn);
  }

  if (parser.isPresent("d"))
  {
    parser.getParameterValue("d", debugMode);
  }
  parser.getParameterValue("t1c", inputT1ce);
  parser.getParameterValue("t1", inputT1);
  parser.getParameterValue("t2", inputT2);
  parser.getParameterValue("fl", inputFlair);
  parser.getParameterValue("o", outputDirectory);

  if (parser.isPresent("m"))
  {
    parser.getParameterValue("m", inputMaskName);
    maskProvided = true;
  }

  if (parser.isPresent("md"))
  {
    parser.getParameterValue("md", modelDirName);
  }

  if (parser.isPresent("ql"))
  {
    parser.getParameterValue("ql", quantLower);
  }
  if (parser.isPresent("qu"))
  {
    parser.getParameterValue("qu", quantUpper);
  }

  if (parser.isPresent("cl"))
  {
    parser.getParameterValue("cl", cutOffLower);
  }
  if (parser.isPresent("cu"))
  {
    parser.getParameterValue("cu", cutOffUpper);
  }

  if (parser.isPresent("t"))
  {
    parser.getParameterValue("t", inferenceType);
  }

  //std::cout << "Input File:" << inputFileName << std::endl;
  //if (!inputMaskName.empty())
  //{
  //  std::cout << "Input Mask:" << inputMaskName << std::endl;
  //}
  //std::cout << "Output File:" << outputFileName << std::endl;
  //std::cout << "Quant Lower:" << quantLower << std::endl;
  //std::cout << "Quant Upper:" << quantUpper << std::endl;
  //std::cout << "CutOff Lower:" << cutOffLower << std::endl;
  //std::cout << "CutOff Upper:" << cutOffUpper << std::endl;

  auto imageInfo = cbica::ImageInfo(inputT1ce);
  
  if (!cbica::ImageSanityCheck(inputT1, inputT1ce))
  {
    std::cerr << "T1 and T1CE images are in inconsistent spaces, please register them before trying.\n";
    return EXIT_FAILURE;
  }
  if (!cbica::ImageSanityCheck(inputT2, inputT1ce))
  {
    std::cerr << "T2 and T1CE images are in inconsistent spaces, please register them before trying.\n";
    return EXIT_FAILURE;
  }
  if (!cbica::ImageSanityCheck(inputFlair, inputT1ce))
  {
    std::cerr << "T2-Flair and T1CE images are in inconsistent spaces, please register them before trying.\n";
    return EXIT_FAILURE;
  }

  if (maskProvided)
  {
    if(!cbica::ImageSanityCheck(inputT1ce, inputMaskName))
    {
      return EXIT_FAILURE;
    }
  }

  switch (imageInfo.GetImageDimensions())
  {
  case 3:
  {
    using ImageType = itk::Image< float, 3 >;
    algorithmRunner< ImageType >();

    break;
  }
  default:
    std::cerr << "Only 2D images are supported right now.\n";
    return EXIT_FAILURE;
  }


  std::cout << "Finished successfully.\n";
  return EXIT_SUCCESS;
}