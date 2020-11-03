#include "cbicaCmdParser.h"
#include "cbicaLogging.h"
#include "cbicaITKSafeImageIO.h"
#include "cbicaUtilities.h"
#include "cbicaITKUtilities.h"

#include "LibraPreprocess.h"

#include "ZScoreNormalizer.h"
#include "FeatureExtraction.h"

std::string inputImageFile, outputDir, parameterFile, logFile;

bool debugMode;

size_t resizingFactor = 100;

std::string findRelativeApplicationPath(const std::string appName)
{
  std::string winExt =
#if WIN32
    ".exe";
#else
    "";
#endif

  if (appName.find("libra") != std::string::npos)
  {
#if WIN32
    winExt = ".bat";
#endif
  }

  auto currentApplicationPath = cbica::normPath(cbica::getExecutablePath()) + "/";

  auto appName_path = cbica::normPath(currentApplicationPath +
#ifndef __APPLE__
    appName + winExt
#else
    "../Resources/bin/" + appName_wrap
#endif  
  );

  if (!cbica::isFile(appName_path))
  {
    std::cerr << "Please install CaPTk properly (LIBRA executable needs to be in the same location as current executable).\n";
    exit(EXIT_FAILURE);
  }
  return appName_path;
}

inline std::string getCaPTkDataDir()
{
  auto captk_currentApplicationPath = cbica::normPath(cbica::getExecutablePath());
  if (debugMode)
  {
    std::cout << "Current Application Path: " << captk_currentApplicationPath << "\n";
  }  

  auto captk_dataDir = captk_currentApplicationPath + "../data/";
  if (!cbica::exists(captk_dataDir))
  {
    captk_dataDir = captk_currentApplicationPath + "../../data/";
    if (!cbica::exists(captk_dataDir))
    {
      captk_dataDir = captk_currentApplicationPath + "../Resources/data/";
      if (!cbica::exists(captk_dataDir))
      {
        captk_dataDir = std::string(PROJECT_SOURCE_DIR) + "data/";
        if (!cbica::exists(captk_dataDir))
        {
          std::cerr << "Data Directory not found. Please re-install CaPTk.\n";
          return "";
        }
      }
    }
  }

  return cbica::normPath(captk_dataDir);
}

//template< class TImageType >
int algorithmsRunner()
{
  cbica::Logging logger;
  if (!logFile.empty())
  {
    logger.UseNewFile(logFile);
  }
  if (debugMode)
  {
    logger.Write("Starting pre-processing.");
  }
  if (!cbica::IsDicom(inputImageFile))
  {
    logger.Write("The input image is not a DICOM image; please provide a DICOM image to continue.");
    return EXIT_FAILURE;
  }
  LibraPreprocess< LibraImageType > preprocessingObj;
  preprocessingObj.SetInputFileName(inputImageFile);
  preprocessingObj.SetResizingFactor(resizingFactor);
  if (debugMode)
  {
    preprocessingObj.EnableDebugMode();
  }
  preprocessingObj.Update();
  if (debugMode)
  {
    logger.Write("Done.");
  }

  auto libraPath = findRelativeApplicationPath("libra");
  //auto libraPath = cbica::normPath("C:/Projects/CaPTk/src/applications/individualApps/libra/libra.bat");
  cbica::createDir(outputDir + "/temp");

  std::string command = libraPath + " " + inputImageFile + " " + outputDir + "/temp/" + cbica::getFilenameBase(inputImageFile)
#if WIN32
    + " true true"
#endif
    ;
  {
    logger.Write("Running LIBRA Single Image with command'" + command + "'");
  }
  std::system(command.c_str());
  auto outputTotalMask = outputDir + "/temp/" + cbica::getFilenameBase(inputImageFile) + "/Result_Images/totalmask/totalmask.dcm";
  if (cbica::isFile(outputTotalMask))
  {
    logger.Write("Done");

    //auto outputTotalMaskImage = cbica::ReadImage< LibraImageType >(outputTotalMask);
    auto dicomReader = itk::ImageSeriesReader< LibraImageType >::New();
    dicomReader->SetImageIO(itk::GDCMImageIO::New());
    dicomReader->SetFileName(outputTotalMask);
    try
    {
      dicomReader->Update();
    }
    catch (itk::ExceptionObject & err)
    {
      logger.Write("Error while loading DICOM image(s): " + std::string(err.what()));
    }
    auto outputTotalMaskImage = dicomReader->GetOutput();

    auto outputRelevantMaskImage = cbica::ChangeImageValues< LibraImageType >(outputTotalMaskImage, "2", "1");
    auto outputRelevantMaskImage_flipped = preprocessingObj.ApplyFlipToMaskImage(outputRelevantMaskImage);

    auto preprocessedImage = preprocessingObj.GetOutputImage();
    logger.Write("Starting z-scoring");
    ZScoreNormalizer< LibraImageType > normalizer;
    normalizer.SetInputImage(preprocessingObj.GetOutputImage());
    normalizer.SetInputMask(outputRelevantMaskImage_flipped);
    normalizer.SetCutoffs(0, 0);
    normalizer.SetQuantiles(0, 0);
    normalizer.Update();

    auto outputFileName = outputDir + "/temp/" + cbica::getFilenameBase(inputImageFile) + "_preprocessed_normalized.nii.gz";

    cbica::WriteImage< LibraImageType >(normalizer.GetOutput(), outputFileName);

    auto outputRelevantMaskFile = outputDir + "/temp/" + cbica::getFilenameBase(inputImageFile) + "_mask.nii.gz";
    cbica::WriteImage< LibraImageType >(outputRelevantMaskImage_flipped, outputRelevantMaskFile);

    logger.Write("Done");
    //auto featureExtractionPath = findRelativeApplicationPath("FeatureExtraction");

    auto currentDataDir = getCaPTkDataDir();
    if (parameterFile.empty())
    {
      parameterFile = currentDataDir + "/features/2_params_default_lattice.csv";
    }
    if (!cbica::isFile(parameterFile))
    {
      std::cerr << "The specified lattice parameter file, '" << parameterFile << "' was not found; please check.\n";
      exit(EXIT_FAILURE);
    }

    logger.Write("Running CaPTk's FeatureExtraction");
    logger.Write("Parameter file: " + parameterFile);

    std::vector< LibraImageType::Pointer > inputImages;
    inputImages.push_back(normalizer.GetOutput());
    FeatureExtraction< LibraImageType > features;
    features.SetPatientID("Lattice");
    features.EnableDebugMode();
    features.SetInputImages(inputImages, "MAM");
    features.SetSelectedROIsAndLabels("1", "Breast");
    features.SetMaskImage(outputRelevantMaskImage_flipped);
    features.SetWriteFeatureMaps(true);
    features.SetValidMask();
    features.SetRequestedFeatures(parameterFile);
    features.SetOutputFilename(cbica::normPath(outputDir + "/features/output.csv"));
    features.SetVerticallyConcatenatedOutput(true);
    features.Update();

    logger.Write("Done");

    return EXIT_SUCCESS;
  }
  else
  {
    std::cerr << "Libra did not succeed. Please recheck.\n";
    return EXIT_FAILURE;
  }
}


int main(int argc, char** argv)
{
  cbica::CmdParser parser(argc, argv);

  parser.addRequiredParameter("i", "inputImage", cbica::Parameter::FILE, "DICOM", "Input Image for processing");
  parser.addRequiredParameter("o", "outputDir", cbica::Parameter::DIRECTORY, "NIfTI", "Dir with write access", "All output files are written here");
  parser.addOptionalParameter("d", "debugMode", cbica::Parameter::BOOLEAN, "0 or 1", "Enabled debug mode", "Default: 0");
  parser.addOptionalParameter("r", "resize", cbica::Parameter::INTEGER, "0 - 100", "What resizing factor is to be applied", "Default: " + std::to_string(resizingFactor));
  parser.addOptionalParameter("p", "paramFile", cbica::Parameter::FILE, ".csv", "A csv file with all features and its parameters filled", "Default: '../data/2_params_default.csv'");
  parser.addOptionalParameter("L", "logDir", cbica::Parameter::DIRECTORY, "Dir with Write access", "A folder to put log files in for additional debugging");

  parser.getParameterValue("i", inputImageFile);
  parser.getParameterValue("o", outputDir);

  parser.addApplicationDescription("This application uses LIBRA to extract the breast mask and then perform feature extraction");
  parser.addExampleUsage("-i C:/test/Case1.dcm -o C:/outputDir -d 1", "This command takes the input mammogram and estimates breast density");

  cbica::createDir(outputDir);
  inputImageFile = cbica::normPath(inputImageFile);
  outputDir = cbica::normPath(outputDir);

  if (parser.isPresent("d"))
  {
    parser.getParameterValue("d", debugMode);
  }
  if (parser.isPresent("r"))
  {
    parser.getParameterValue("r", resizingFactor);
  }
  if (parser.isPresent("p"))
  {
    parser.getParameterValue("p", parameterFile);
  }
  if (parser.isPresent("L"))
  {
    std::string tempLogDir;
    parser.getParameterValue("L", tempLogDir);
    cbica::createDir(tempLogDir);
    logFile = cbica::normPath(tempLogDir + "/" + cbica::getCurrentLocalTimestamp() + ".log");
  }
  //auto inputImageInfo = cbica::ImageInfo(inputImageFile);

  //switch (inputImageInfo.GetImageDimensions())
  //{
  //case 2:
  //{
    //using ImageType = itk::Image< float, 2 >;
  return algorithmsRunner();

  //  break;
  //}
  //default:
  //  std::cerr << "Supplied image has an unsupported dimension of '" << inputImageInfo.GetImageDimensions() << "'; only 2 D images are supported.\n";
  //  return EXIT_FAILURE; // exiting here because no further processing should be done on the image
  //}

  return EXIT_SUCCESS;
}