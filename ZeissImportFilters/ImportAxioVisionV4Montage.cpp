/*
 * Your License or Copyright Information can go here
 */

#include "ImportAxioVisionV4Montage.h"

#include <array>
#include <cstring>
#include <limits>
#include <set>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSharedPointer>
#include <QtCore/QString>
#include <QtCore/QTextStream>
#include <QtCore/QVector>

#include "SIMPLib/DataArrays/StringDataArray.h"
#include "SIMPLib/FilterParameters/AbstractFilterParametersReader.h"
#include "SIMPLib/FilterParameters/FloatVec3FilterParameter.h"
#include "SIMPLib/FilterParameters/InputFileFilterParameter.h"
#include "SIMPLib/FilterParameters/LinkedBooleanFilterParameter.h"
#include "SIMPLib/FilterParameters/StringFilterParameter.h"
#include "SIMPLib/Filtering/FilterManager.h"
#include "SIMPLib/Geometry/ImageGeom.h"

#include "ZeissImport/ZeissImportConstants.h"
#include "ZeissImport/ZeissImportVersion.h"
#include "ZeissImport/ZeissXml/ZeissTagMapping.h"

#define ZIF_PRINT_DBG_MSGS 0

static const QString k_AttributeArrayNames("AttributeArrayNames");
static const QString k_DataContaineNameDefaultName("Zeiss Axio Vision Montage");
static const QString k_TileAttributeMatrixDefaultName("Tile Data");
static const QString k_GrayScaleTempArrayName("gray_scale_temp");
static const QString k_AxioVisionMetaData("AxioVision MetaData");

/* ############## Start Private Implementation ############################### */
// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
class ImportAxioVisionV4MontagePrivate
{
  Q_DISABLE_COPY(ImportAxioVisionV4MontagePrivate)
  Q_DECLARE_PUBLIC(ImportAxioVisionV4Montage)
  ImportAxioVisionV4Montage* const q_ptr;
  ImportAxioVisionV4MontagePrivate(ImportAxioVisionV4Montage* ptr);

  QDomElement m_Root;
  ZeissTagsXmlSection::Pointer m_RootTagsSection;
  QString m_InputFile_Cache;
  QDateTime m_LastRead;
};

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
ImportAxioVisionV4MontagePrivate::ImportAxioVisionV4MontagePrivate(ImportAxioVisionV4Montage* ptr)
: q_ptr(ptr)
, m_Root(QDomElement())
, m_InputFile_Cache("")
, m_LastRead(QDateTime())
{
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
ImportAxioVisionV4Montage::ImportAxioVisionV4Montage()
: m_InputFile("")
, m_DataContainerName(k_DataContaineNameDefaultName)
, m_CellAttributeMatrixName(k_TileAttributeMatrixDefaultName)
, m_ImageDataArrayName(SIMPL::CellData::ImageData)
, m_MetaDataAttributeMatrixName(k_AxioVisionMetaData)
, m_FileWasRead(false)
, d_ptr(new ImportAxioVisionV4MontagePrivate(this))
{
  m_ColorWeights.x = 0.2125f;
  m_ColorWeights.y = 0.7154f;
  m_ColorWeights.z = 0.0721f;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
ImportAxioVisionV4Montage::~ImportAxioVisionV4Montage() = default;

SIMPL_PIMPL_PROPERTY_DEF(ImportAxioVisionV4Montage, QString, InputFile_Cache)
SIMPL_PIMPL_PROPERTY_DEF(ImportAxioVisionV4Montage, QDateTime, LastRead)
SIMPL_PIMPL_PROPERTY_DEF(ImportAxioVisionV4Montage, QDomElement, Root)
SIMPL_PIMPL_PROPERTY_DEF(ImportAxioVisionV4Montage, ZeissTagsXmlSection::Pointer, RootTagsSection)

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::setupFilterParameters()
{
  FilterParameterVector parameters;
  parameters.push_back(SIMPL_NEW_INPUT_FILE_FP("Zeiss XML File (_meta.xml)", InputFile, FilterParameter::Parameter, ImportAxioVisionV4Montage, "*.xml"));
  QStringList linkedProps("ColorWeights");
  parameters.push_back(SIMPL_NEW_LINKED_BOOL_FP("Convert To GrayScale", ConvertToGrayScale, FilterParameter::Parameter, ImportAxioVisionV4Montage, linkedProps));
  parameters.push_back(SIMPL_NEW_FLOAT_VEC3_FP("Color Weighting", ColorWeights, FilterParameter::Parameter, ImportAxioVisionV4Montage));

  parameters.push_back(SIMPL_NEW_STRING_FP("DataContainer Prefix", DataContainerName, FilterParameter::CreatedArray, ImportAxioVisionV4Montage));
  parameters.push_back(SIMPL_NEW_STRING_FP("Cell Attribute Matrix Name", CellAttributeMatrixName, FilterParameter::CreatedArray, ImportAxioVisionV4Montage));
  parameters.push_back(SIMPL_NEW_STRING_FP("Image DataArray Name", ImageDataArrayName, FilterParameter::CreatedArray, ImportAxioVisionV4Montage));
  parameters.push_back(SIMPL_NEW_STRING_FP("MetaData AttributeMatrix Name", MetaDataAttributeMatrixName, FilterParameter::CreatedArray, ImportAxioVisionV4Montage));

  setFilterParameters(parameters);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::readFilterParameters(AbstractFilterParametersReader* reader, int index)
{
  reader->openFilterGroup(this, index);
  setInputFile(reader->readString("InputFile", getInputFile()));
  setDataContainerName(reader->readString("DataContainerName", getDataContainerName()));
  setCellAttributeMatrixName(reader->readString("CellAttributeMatrixName", getCellAttributeMatrixName()));
  setImageDataArrayName(reader->readString("ImageDataArrayName", getImageDataArrayName()));
  setConvertToGrayScale(reader->readValue("ConvertToGrayScale", getConvertToGrayScale()));
  setColorWeights(reader->readFloatVec3("ColorWeights", getColorWeights()));
  reader->closeFilterGroup();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::readFilterParameters(QJsonObject& obj)
{
  AbstractFilter::readFilterParameters(obj);
  setImageDataArrayName(obj["ImageDataArrayName"].toString());
}

// FP: Check why these values are not connected to a filter parameter!

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::writeFilterParameters(QJsonObject& obj) const
{
  AbstractFilter::writeFilterParameters(obj);
  obj["ImageDataArrayName"] = getImageDataArrayName();
}
// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::initialize()
{
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::dataCheck()
{
  setErrorCondition(0);
  setWarningCondition(0);

  QString ss;
  QFileInfo fi(getInputFile());
  if(getInputFile().isEmpty())
  {
    ss = QObject::tr("%1 needs the Input File Set and it was not.").arg(ClassName());
    setErrorCondition(-387);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
  }
  else if(!fi.exists())
  {
    ss = QObject::tr("The input file does not exist.");
    setErrorCondition(-388);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
  }

  QString filtName = ZeissImportConstants::ImageProcessingFilters::k_ReadImageFilterClassName;
  FilterManager* fm = FilterManager::Instance();
  IFilterFactory::Pointer filterFactory = fm->getFactoryFromClassName(filtName);
  if(nullptr != filterFactory.get())
  {
    // If we get this far, the Factory is good so creating the filter should not fail unless something has
    // horribly gone wrong in which case the system is going to come down quickly after this.
    AbstractFilter::Pointer filter = filterFactory->create();
    if(nullptr == filter.get())
    {
      ss = QObject::tr("The '%1' filter is not Available, did the ITKImageProcessing Plugin Load.").arg(filtName);
      setErrorCondition(-391);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }
  }
  if(getConvertToGrayScale())
  {
    QString filtName = ZeissImportConstants::ImageProcessingFilters::k_RgbToGrayFilterClassName;
    FilterManager* fm = FilterManager::Instance();
    IFilterFactory::Pointer filterFactory = fm->getFactoryFromClassName(filtName);
    if(nullptr != filterFactory.get())
    {
      // If we get this far, the Factory is good so creating the filter should not fail unless something has
      // horribly gone wrong in which case the system is going to come down quickly after this.
      AbstractFilter::Pointer filter = filterFactory->create();
      if(nullptr == filter.get())
      {
        ss = QObject::tr("The '%1' filter is not Available, did the Processing Plugin Load.").arg(filtName);
        setErrorCondition(-391);
        notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
      }
    }
  }
  if(getErrorCondition() < 0)
  {
    return;
  }

  DataContainerArray::Pointer dca = getDataContainerArray();
  if(nullptr == dca.get())
  {
    ss = QObject::tr("%1 needs a valid DataContainerArray").arg(ClassName());
    setErrorCondition(-390);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return;
  }

  // Parse the XML file to get all the meta-data information and create all the
  // data structure that is needed.
  QFile xmlFile(getInputFile());
  readMetaXml(&xmlFile);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::preflight()
{
  // These are the REQUIRED lines of CODE to make sure the filter behaves correctly
  setInPreflight(true);              // Set the fact that we are preflighting.
  emit preflightAboutToExecute();    // Emit this signal so that other widgets can do one file update
  emit updateFilterParameters(this); // Emit this signal to have the widgets push their values down to the filter
  dataCheck();                       // Run our DataCheck to make sure everthing is setup correctly
  emit preflightExecuted();          // We are done preflighting this filter
  setInPreflight(false);             // Inform the system this filter is NOT in preflight mode anymore.
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::execute()
{
  int err = 0;
  // typically run your dataCheck function to make sure you can get that far and all your variables are initialized
  dataCheck();
  // Check to make sure you made it through the data check. Errors would have been reported already so if something
  // happens to fail in the dataCheck() then we simply return
  if(getErrorCondition() < 0)
  {
    return;
  }
  setErrorCondition(0);
  setWarningCondition(0);

  /* If some error occurs this code snippet can report the error up the call chain*/
  if(err < 0)
  {
    QString ss = QObject::tr("Error Importing a Zeiss AxioVision file set.");
    setErrorCondition(-90000);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return;
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::readMetaXml(QIODevice* device)
{
  QString errorStr;
  int errorLine;
  int errorColumn;

  QFileInfo fi(getInputFile());
  QDateTime lastModified(fi.lastModified());

  QDomDocument domDocument;
  QDomElement root;
  ZeissTagsXmlSection::Pointer rootTagsSection;

  if(getInputFile() == getInputFile_Cache() && getLastRead().isValid() && lastModified.msecsTo(getLastRead()) >= 0)
  {
    // We are reading from the cache, so set the FileWasRead flag to false
    m_FileWasRead = false;
    root = getRoot();
    rootTagsSection = getRootTagsSection();
  }
  else
  {
    // We are reading from the file, so set the FileWasRead flag to true
    m_FileWasRead = true;

    if(!domDocument.setContent(device, true, &errorStr, &errorLine, &errorColumn))
    {
      QString ss = QObject::tr("Parse error at line %1, column %2:\n%3").arg(errorLine).arg(errorColumn).arg(errorStr);
      setErrorCondition(-70000);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
      return;
    }

    root = domDocument.documentElement();

    QDomElement tags = root.firstChildElement(ZeissImportConstants::Xml::Tags);
    if(tags.isNull())
    {
      QString ss = QObject::tr("Could not find the <ROOT><Tags> element. Aborting Parsing. Is the file a Zeiss _meta.xml file");
      setErrorCondition(-70001);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
      return;
    }

    // First parse the <ROOT><Tags> section to get the values of how many images we are going to have
    rootTagsSection = parseTagsSection(tags);
    if(nullptr == rootTagsSection.get())
    {
      return;
    }

    // Set the data into the cache
    setRootTagsSection(rootTagsSection);

    QDomElement rootCopy = root.cloneNode().toElement();
    setRoot(rootCopy);

    // Set the file path and time stamp into the cache
    setLastRead(QDateTime::currentDateTime());
    setInputFile_Cache(getInputFile());
  }

  // Now parse each of the <pXXX> tags
  parseImages(root, rootTagsSection);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
ZeissTagsXmlSection::Pointer ImportAxioVisionV4Montage::parseTagsSection(QDomElement& tags)
{
  int count = -1;
  bool ok = false;

  QDomElement countEle = tags.firstChildElement(ZeissImportConstants::Xml::Count);

  count = countEle.text().toInt(&ok, 10);
  if(!ok)
  {
    QString ss = QObject::tr("Error Parsing 'Count' Tag in Root 'Tags' DOM element");
    setErrorCondition(-70001);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return ZeissTagsXmlSection::NullPointer();
  }

  ZeissTagsXmlSection::Pointer rootTagsSection = ZeissTagsXmlSection::New();
  ZeissTagMapping::Pointer tagMapping = ZeissTagMapping::instance();

  std::set<int> unknownTags;

  for(int c = 0; c < count; c++)
  {
    QString Vx = QString("V%1").arg(c);
    QString Ix = QString("I%1").arg(c);
    // QString Ax = QString("A%1").arg(c);

    QDomElement vxEle = tags.firstChildElement(Vx);
    QDomElement ixEle = tags.firstChildElement(Ix);

    qint32 idValue = ixEle.text().toInt(&ok, 10);
    AbstractZeissMetaData::Pointer ptr = ZeissTagMapping::instance()->metaDataForId(idValue, vxEle.text());
    if(nullptr != ptr.get() && vxEle.text().size() > 0)
    {
      rootTagsSection->addMetaDataEntry(ptr);
    }
#if ZIF_PRINT_DBG_MSGS
    else
    {
      unknownTags.insert(idValue);
      //      QString str;
      //      QTextStream ss(&str);
      //      ss << "<" << Ix << ">" << idValue << "</" << Ix << "> is Unknown to the Tag Mapping Software";
      //      qDebug() << str;
    }
#endif
  }

#if ZIF_PRINT_DBG_MSGS

  if(unknownTags.size() > 0)
  {
    QString str;
    QTextStream ss(&str);
    ss << "======= Unknown Zeiss Axio Vision _Meta XML Tags ===================\n";
    for(std::set<int>::iterator iter = unknownTags.begin(); iter != unknownTags.end(); ++iter)
    {
      ss << *iter << " is Unknown to the Tag Mapping Software\n";
    }
    qDebug() << str;
  }
#endif

  return rootTagsSection;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
int32_t getInt32Entry(AbstractFilter* filter, const ZeissTagsXmlSection::Pointer& tagsSection, int idValue)
{
  AbstractZeissMetaData::Pointer ptr = tagsSection->getEntry(idValue);
  if(nullptr == ptr)
  {
    filter->setErrorCondition(-70600);
    QString msg;
    QTextStream errStrm(&msg);
    errStrm << "AxioVision Import: XML Section for '" << ZeissTagMapping::instance()->nameForId(idValue) << "' was not found.";
    filter->notifyErrorMessage(filter->getHumanLabel(), msg, filter->getErrorCondition());
    return 0;
  }

  Int32ZeissMetaEntry::Pointer valuePtr = ZeissMetaEntry::convert<Int32ZeissMetaEntry>(ptr);
  if(nullptr == valuePtr)
  {
    filter->setErrorCondition(-70601);
    QString msg;
    QTextStream errStrm(&msg);
    errStrm << "AxioVision Import: Could not convert '" << ZeissTagMapping::instance()->nameForId(idValue) << "' tag to an integer.";
    filter->notifyErrorMessage(filter->getHumanLabel(), msg, filter->getErrorCondition());
    return 0;
  }

  return valuePtr->getValue();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
float getFloatEntry(AbstractFilter* filter, const ZeissTagsXmlSection::Pointer& tagsSection, int idValue)
{
  AbstractZeissMetaData::Pointer ptr = tagsSection->getEntry(idValue);
  if(nullptr == ptr)
  {
    filter->setErrorCondition(-70602);
    QString msg;
    QTextStream errStrm(&msg);
    errStrm << "AxioVision Import: XML Section for '" << ZeissTagMapping::instance()->nameForId(idValue) << "' was not found.";
    filter->notifyErrorMessage(filter->getHumanLabel(), msg, filter->getErrorCondition());
    return std::numeric_limits<float>::quiet_NaN();
  }

  FloatZeissMetaEntry::Pointer valuePtr = ZeissMetaEntry::convert<FloatZeissMetaEntry>(ptr);
  if(nullptr == valuePtr)
  {
    filter->setErrorCondition(-70603);
    QString msg;
    QTextStream errStrm(&msg);
    errStrm << "AxioVision Import: Could not convert '" << ZeissTagMapping::instance()->nameForId(idValue) << "' tag to a float.";
    filter->notifyErrorMessage(filter->getHumanLabel(), msg, filter->getErrorCondition());
    return std::numeric_limits<float>::quiet_NaN();
  }

  return valuePtr->getValue();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
int32_t calculatePaddingDigits(int32_t count)
{
  int zeroPadding = 0;
  if(count > 0)
  {
    zeroPadding++;
  }
  if(count > 9)
  {
    zeroPadding++;
  }
  if(count > 99)
  {
    zeroPadding++;
  }
  if(count > 999)
  {
    zeroPadding++;
  }
  if(count > 9999)
  {
    zeroPadding++;
  }
  return zeroPadding;
}
// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::parseImages(QDomElement& root, const ZeissTagsXmlSection::Pointer& rootTagsSection)
{

  int32_t imageCount = getInt32Entry(this, rootTagsSection, Zeiss::MetaXML::ImageCountRawId);
  if(getErrorCondition() < 0)
  {
    return;
  }

  int32_t rowCount = getInt32Entry(this, rootTagsSection, Zeiss::MetaXML::ImageCountVId);
  if(getErrorCondition() < 0)
  {
    return;
  }

  int32_t colCount = getInt32Entry(this, rootTagsSection, Zeiss::MetaXML::ImageCountUId);
  if(getErrorCondition() < 0)
  {
    return;
  }

  AbstractZeissMetaData::Pointer fileNamePtr = rootTagsSection->getEntry(Zeiss::MetaXML::FilenameId);
  StringZeissMetaEntry::Pointer imageNamePtr = std::dynamic_pointer_cast<StringZeissMetaEntry>(fileNamePtr);
  QString imageName = imageNamePtr->getValue();

  // Figure out the max padding digits for both the imageCount (we need that to generate the proper xml tag) and
  // the row/col values because we need to have a consistent numbering format for later filters.
  int imageCountPadding = calculatePaddingDigits(imageCount);
  int32_t rowCountPadding = calculatePaddingDigits(rowCount);
  int32_t colCountPadding = calculatePaddingDigits(colCount);
  int charPaddingCount = std::max(rowCountPadding, colCountPadding);

  DataContainerArray::Pointer dca = getDataContainerArray();

  // Loop over every image in the _meta.xml file
  for(int p = 0; p < imageCount; p++)
  {
    // Generate the xml tag that is for this image
    QString pTag;
    QTextStream out(&pTag);
    out << "p";
    out.setFieldWidth(imageCountPadding);
    out.setFieldAlignment(QTextStream::AlignRight);
    out.setPadChar('0');
    out << p;

    // Send a status update on the progress
    QString msg = QString("%1: Importing file %2 of %3").arg(getHumanLabel()).arg(p).arg(imageCount);
    notifyStatusMessage(getHumanLabel(), msg);

    // Drill down into the XML document....
    QDomElement photoEle = root.firstChildElement(pTag);
    if(photoEle.isNull())
    {
      QString ss = QObject::tr("Could not find the <ROOT><%1> element. Aborting Parsing. Is the file a Zeiss _meta.xml file").arg(pTag);
      setErrorCondition(-70002);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
      return;
    }
    // Get the TAGS section
    QDomElement tags = photoEle.firstChildElement(ZeissImportConstants::Xml::Tags);
    if(tags.isNull())
    {
      QString ss = QObject::tr("Could not find the <ROOT><%1><Tags> element. Aborting Parsing. Is the file a Zeiss _meta.xml file").arg(pTag);
      setErrorCondition(-70003);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
      return;
    }
    // Now Parse the TAGS section
    ZeissTagsXmlSection::Pointer photoTagsSection = parseTagsSection(tags);
    if(nullptr == photoTagsSection.get())
    {
      QString ss = QObject::tr("Error Parsing the <ROOT><%1><Tags> element. Aborting Parsing. Is the file a Zeiss AxioVision _meta.xml file").arg(pTag);
      setErrorCondition(-70004);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
      return;
    }

    // Get the Row Index (Zero Based)
    int32_t rowIndex = getInt32Entry(this, photoTagsSection, Zeiss::MetaXML::ImageIndexVId);
    if(getErrorCondition() < 0)
    {
      return;
    }
    // Get the Columnn Index (Zero Based)
    int32_t colIndex = getInt32Entry(this, photoTagsSection, Zeiss::MetaXML::ImageIndexUId);
    if(getErrorCondition() < 0)
    {
      return;
    }

    // Create our DataContainer Name using a Prefix and a rXXcYY format.
    QString dcName = getDataContainerName();
    QTextStream dcNameStream(&dcName);
    dcNameStream << "_r";
    dcNameStream.setFieldWidth(charPaddingCount);
    dcNameStream.setFieldAlignment(QTextStream::AlignRight);
    dcNameStream.setPadChar('0');
    dcNameStream << rowIndex;
    dcNameStream.setFieldWidth(0);
    dcNameStream << "c";
    dcNameStream.setFieldWidth(charPaddingCount);
    dcNameStream << colIndex;

    // Create the DataContainer with a name based on the ROW & COLUMN indices
    DataContainer::Pointer dc = dca->createNonPrereqDataContainer<AbstractFilter>(this, dcName);

    // Create the Image Geometry
    ImageGeom::Pointer image = initializeImageGeom(root, photoTagsSection);
    dc->setGeometry(image);

    QVector<size_t> dims = {1};
    AttributeMatrix::Pointer metaAm = dc->createAndAddAttributeMatrix(dims, getMetaDataAttributeMatrixName(), AttributeMatrix::Type::Generic);
    ZeissTagsXmlSection::MetaDataType tagMap = photoTagsSection->getMetaDataMap();
    for(const auto& value : tagMap)
    {
      IDataArray::Pointer dataArray = value->createDataArray(!getInPreflight());
      metaAm->addAttributeArray(dataArray->getName(), dataArray);
    }

    // Read the image into a data array
    importImage(dc.get(), imageName, pTag, p);

    if(getConvertToGrayScale())
    {
      convertToGrayScale(dc.get(), imageName, pTag);
    }
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::addMetaData(const AttributeMatrix::Pointer& metaAm, const ZeissTagsXmlSection::Pointer& photoTagsSection, int index)
{
  ZeissTagMapping::Pointer tagMap = ZeissTagMapping::instance();

  ZeissTagsXmlSection::MetaDataType tags = photoTagsSection->getMetaDataMap();

  QMapIterator<int, AbstractZeissMetaData::Pointer> iter(tags);
  while(iter.hasNext())
  {
    iter.next();
    StringZeissMetaEntry::Pointer zStrVal = std::dynamic_pointer_cast<StringZeissMetaEntry>(iter.value());

    QString tagName = tagMap->nameForId(iter.key());

    IDataArray::Pointer iDataArray = metaAm->getAttributeArray(tagName);
    StringDataArray::Pointer strArray = std::dynamic_pointer_cast<StringDataArray>(iDataArray);
    strArray->setValue(index, zStrVal->getValue());
    // IDataArray::Pointer dataArray = iter.value()->createDataArray(!getInPreflight());
    // metaData->addAttributeArray(dataArray->getName(), dataArray);
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::addRootMetaData(const AttributeMatrix::Pointer& metaAm, const ZeissTagsXmlSection::Pointer& rootTagsSection, int index)
{
  ZeissTagMapping::Pointer tagMap = ZeissTagMapping::instance();
  ZeissTagsXmlSection::MetaDataType tags = rootTagsSection->getMetaDataMap();

  QMapIterator<int, AbstractZeissMetaData::Pointer> iter(tags);
  while(iter.hasNext())
  {
    iter.next();
    StringZeissMetaEntry::Pointer zStrVal = std::dynamic_pointer_cast<StringZeissMetaEntry>(iter.value());

    QString tagName = tagMap->nameForId(iter.key());
    if(tagName == "ScaleFactorForX" || tagName == "ScaleFactorForY")
    {
      IDataArray::Pointer iDataArray = metaAm->getAttributeArray(tagName);
      StringDataArray::Pointer strArray = std::dynamic_pointer_cast<StringDataArray>(iDataArray);
      strArray->setValue(index, zStrVal->getValue());
      // IDataArray::Pointer dataArray = iter.value()->createDataArray(!getInPreflight());
      // metaData->addAttributeArray(dataArray->getName(), dataArray);
    }
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void ImportAxioVisionV4Montage::importImage(DataContainer* dc, const QString& imageName, const QString& pTag, int imageIndex)
{

  QFileInfo fi(imageName);
  QString imagePath = fi.completeBaseName() + "_" + pTag + "." + fi.suffix();
  // QString dataArrayName = fi.completeBaseName() + "_" + pTag;
  QString imageDataArrayNamme = getImageDataArrayName();

  // Add this Array name to the DataArray that will ensure the correct ordering
  // attributeArrayNames->setValue(imageIndex, dataArrayName);

  fi = QFileInfo(getInputFile());
  imagePath = fi.absoluteDir().path() + "/" + imagePath;
  //   std::string sPath = imagePath.toStdString();

  QString filtName = ZeissImportConstants::ImageProcessingFilters::k_ReadImageFilterClassName;
  FilterManager* fm = FilterManager::Instance();
  IFilterFactory::Pointer filterFactory = fm->getFactoryFromClassName(filtName);
  if(nullptr != filterFactory.get())
  {
    // If we get this far, the Factory is good so creating the filter should not fail unless something has
    // horribly gone wrong in which case the system is going to come down quickly after this.
    AbstractFilter::Pointer filter = filterFactory->create();

    // Connect up the Error/Warning/Progress object so the filter can report those things
    connect(filter.get(), SIGNAL(filterGeneratedMessage(const PipelineMessage&)), this, SLOT(broadcastPipelineMessage(const PipelineMessage&)));
    filter->setMessagePrefix(getMessagePrefix());
    DataContainerArray::Pointer dca = DataContainerArray::New();

    filter->setDataContainerArray(dca); // AbstractFilter implements this so no problem

    bool propWasSet = filter->setProperty("FileName", imagePath);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("InputFileName", filtName, getHumanLabel());
      setErrorCondition(-70015);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    propWasSet = filter->setProperty("DataContainerName", dc->getName());
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("DataContainerName", filtName, getHumanLabel());
      setErrorCondition(-70016);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    propWasSet = filter->setProperty("CellAttributeMatrixName", getCellAttributeMatrixName());
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("CellAttributeMatrixName", filtName, getHumanLabel());
      setErrorCondition(-70017);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    propWasSet = filter->setProperty("ImageDataArrayName", imageDataArrayNamme);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("ImageDataArrayName", filtName, getHumanLabel());
      setErrorCondition(-70018);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    if(getInPreflight())
    {
      filter->preflight();
    }
    else
    {
      filter->execute();
    }

    if(filter->getErrorCondition() >= 0)
    {
      //  Copy the image from the temp data container into the current data container by grabbing the entire
      // Cell AttributeMatrix
      AttributeMatrix::Pointer cellAttrMat = dca->getDataContainer(dc->getName())->getAttributeMatrix(getCellAttributeMatrixName());
      dc->addAttributeMatrix(cellAttrMat->getName(), cellAttrMat);
    }
  }
  else
  {
    QString ss = QObject::tr("Error trying to instantiate the '%1' filter which is typically included in the 'ImageProcessing' plugin.")
                     .arg(ZeissImportConstants::ImageProcessingFilters::k_ReadImageFilterClassName);
    setErrorCondition(-70019);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return;
  }
}

// -----------------------------------------------------------------------------
//
// -------------------------------------------------------------------------
void ImportAxioVisionV4Montage::convertToGrayScale(DataContainer* dc, const QString& imageName, const QString& pTag)
{

  QString filtName = ZeissImportConstants::ImageProcessingFilters::k_RgbToGrayFilterClassName;
  FilterManager* fm = FilterManager::Instance();
  IFilterFactory::Pointer filterFactory = fm->getFactoryFromClassName(filtName);
  if(nullptr != filterFactory.get())
  {
    // If we get this far, the Factory is good so creating the filter should not fail unless something has
    // horribly gone wrong in which case the system is going to come down quickly after this.
    AbstractFilter::Pointer filter = filterFactory->create();

    // Connect up the Error/Warning/Progress object so the filter can report those things
    connect(filter.get(), SIGNAL(filterGeneratedMessage(const PipelineMessage&)), this, SLOT(broadcastPipelineMessage(const PipelineMessage&)));
    filter->setMessagePrefix(getMessagePrefix());
    filter->setDataContainerArray(getDataContainerArray()); // AbstractFilter implements this so no problem

    DataArrayPath dap(dc->getName(), getCellAttributeMatrixName(), getImageDataArrayName());

    QVariant variant;
    bool propWasSet = false;

    //----- Set the Conversion Algorithm property
    variant.setValue(0); // Luminosity
    propWasSet = filter->setProperty("ConversionAlgorithm", variant);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("ConversionAlgorithm", filtName, getHumanLabel());
      setErrorCondition(-70002);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    //----- Set the ColorWeights Property
    variant.setValue(getColorWeights());
    propWasSet = filter->setProperty("ColorWeights", variant);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("ColorWeights", filtName, getHumanLabel());
      setErrorCondition(-70003);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    //------- Set the InputDataArrayVector property
    QVector<DataArrayPath> inputDataArrayVector = {dap};
    variant.setValue(inputDataArrayVector);
    propWasSet = filter->setProperty("InputDataArrayVector", variant);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("InputDataArrayVector", filtName, getHumanLabel());
      setErrorCondition(-70004);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    //----- Set the CreateNewAttributeMatrix Property
    variant.setValue(false);
    propWasSet = filter->setProperty("CreateNewAttributeMatrix", variant);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("CreateNewAttributeMatrix", filtName, getHumanLabel());
      setErrorCondition(-70005);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    //----- Set the OutputAttributeMatrixName Property
    variant.setValue(getCellAttributeMatrixName());
    propWasSet = filter->setProperty("OutputAttributeMatrixName", variant);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("OutputAttributeMatrixName", filtName, getHumanLabel());
      setErrorCondition(-70006);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }

    //----- Set the OutputArrayPrefix Property
    propWasSet = filter->setProperty("OutputArrayPrefix", k_GrayScaleTempArrayName);
    if(!propWasSet)
    {
      QString ss = QObject::tr("Error Setting Property '%1' into filter '%2' which is a subfilter called by %3. The property was not set which could mean the property was not exposed with a "
                               "Q_PROPERTY macro. Please notify the developers.")
                       .arg("OutputArrayPrefix", filtName, getHumanLabel());
      setErrorCondition(-70007);
      notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    }
    if(getInPreflight())
    {
      filter->preflight();
    }
    else
    {
      filter->execute();
    }

    if(filter->getErrorCondition() >= 0)
    {
      AttributeMatrix::Pointer am = dc->getAttributeMatrix(getCellAttributeMatrixName());
      IDataArray::Pointer rgb = am->removeAttributeArray(getImageDataArrayName());
      IDataArray::Pointer gray = am->removeAttributeArray(k_GrayScaleTempArrayName + getImageDataArrayName());
      gray->setName(rgb->getName());
      am->addAttributeArray(gray->getName(), gray);
    }
  }
  else
  {
    QString ss = QObject::tr("Error trying to instantiate the '%1' filter which is typically included in the 'ImageProcessing' plugin.")
                     .arg(ZeissImportConstants::ImageProcessingFilters::k_RgbToGrayFilterClassName);
    setErrorCondition(-70009);
    notifyErrorMessage(getHumanLabel(), ss, getErrorCondition());
    return;
  }
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
ImageGeom::Pointer ImportAxioVisionV4Montage::initializeImageGeom(const QDomElement& root, const ZeissTagsXmlSection::Pointer& photoTagsSection)
{

  ImageGeom::Pointer image = ImageGeom::CreateGeometry(SIMPL::Geometry::ImageGeometry);

  //#######################################################################
  // Parse out the Pixel Dimensions of the image.
  QVector<size_t> dims(3);
  AbstractZeissMetaData::Pointer ptr = photoTagsSection->getEntry(Zeiss::MetaXML::ImageWidthPixelId);
  Int32ZeissMetaEntry::Pointer int32Entry = ZeissMetaEntry::convert<Int32ZeissMetaEntry>(ptr);
  Q_ASSERT_X(int32Entry.get() != nullptr, "Could not Cast to Int32ZeissMetaEntry", "");

  dims[0] = int32Entry->getValue();

  ptr = photoTagsSection->getEntry(Zeiss::MetaXML::ImageHeightPixelId);
  int32Entry = ZeissMetaEntry::convert<Int32ZeissMetaEntry>(ptr);
  Q_ASSERT_X(int32Entry.get() != nullptr, "Could not Cast to Int32ZeissMetaEntry", "");

  dims[1] = int32Entry->getValue();
  dims[2] = 1;
  image->setDimensions(dims[0], dims[1], 1);

  //#######################################################################
  // Initialize the Origin to the Stage Positions
  float stageXPos = getFloatEntry(this, photoTagsSection, Zeiss::MetaXML::StagePositionXId);
  float stageYPos = getFloatEntry(this, photoTagsSection, Zeiss::MetaXML::StagePositionYId);
  float origin[3] = {stageXPos, stageYPos, 0.0f};
  image->setOrigin(origin);

  //#######################################################################
  // Initialize the Resolution of the geometry
  bool ok = false;
  std::array<float, 3> scaling = {{1.0f, 1.0f, 1.0f}};
  QDomElement scalingDom = root.firstChildElement(ZeissImportConstants::Xml::Scaling).firstChildElement("Factor_0");
  scaling[0] = scalingDom.text().toFloat(&ok);
  scalingDom = root.firstChildElement(ZeissImportConstants::Xml::Scaling).firstChildElement("Factor_1");
  scaling[1] = scalingDom.text().toFloat(&ok);
  image->setResolution(scaling.data());

  //#######################################################################
  // Initialize the Length Units of the geometry
  QDomElement unitsDom = root.firstChildElement(ZeissImportConstants::Xml::Scaling).firstChildElement("Type_0");
  int xUnits = unitsDom.text().toInt(&ok);
  // We are going to assume that the units in both the X and Y are the same. Why would they be different?
  //  unitsDom = root.firstChildElement(ZeissImportConstants::Xml::Scaling).firstChildElement("Type_1");
  //  int yUnits = unitsDom.text().toInt(&ok);

  IGeometry::LengthUnit lengthUnit = ZeissUnitMapping::Instance()->convertToIGeometryLengthUnit(xUnits);
  image->setUnits(lengthUnit);
  image->setName("AxioVision V4 Geometry");

  return image;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString ImportAxioVisionV4Montage::getCompiledLibraryName() const
{
  return ZeissImportConstants::ZeissImportBaseName;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString ImportAxioVisionV4Montage::getBrandingString() const
{
  return "ZeissImport";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString ImportAxioVisionV4Montage::getFilterVersion() const
{
  QString version;
  QTextStream vStream(&version);
  vStream << ZeissImport::Version::Major() << "." << ZeissImport::Version::Minor() << "." << ZeissImport::Version::Patch();
  return version;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString ImportAxioVisionV4Montage::getGroupName() const
{
  return SIMPL::FilterGroups::Unsupported;
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString ImportAxioVisionV4Montage::getHumanLabel() const
{
  return "Zeiss AxioVision Import (V4)";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QUuid ImportAxioVisionV4Montage::getUuid()
{
  return QUuid("{411b008c-006f-51b2-ba05-99e51a01af3c}");
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
const QString ImportAxioVisionV4Montage::getSubGroupName() const
{
  return "IO";
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
AbstractFilter::Pointer ImportAxioVisionV4Montage::newFilterInstance(bool copyFilterParameters) const
{
  ImportAxioVisionV4Montage::Pointer filter = ImportAxioVisionV4Montage::New();
  if(copyFilterParameters)
  {
    copyFilterParameterInstanceVariables(filter.get());
  }
  return filter;
}