#include "qt/about.hpp"
#include "qt/bookmark_dialog.hpp"
#include "qt/draw_widget.hpp"
#include "qt/mainwindow.hpp"
#include "qt/mwms_borders_selection.hpp"
#include "qt/osm_auth_dialog.hpp"
#include "qt/popup_menu_holder.hpp"
#include "qt/preferences_dialog.hpp"
#include "qt/qt_common/helpers.hpp"
#include "qt/qt_common/scale_slider.hpp"
#include "qt/routing_settings_dialog.hpp"
#include "qt/screenshoter.hpp"
#include "qt/search_panel.hpp"

#include "platform/settings.hpp"
#include "platform/platform.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include "defines.hpp"

#include <functional>
#include <sstream>

#include "std/target_os.hpp"

#ifdef BUILD_DESIGNER
#include "build_style/build_common.h"
#include "build_style/build_phone_pack.h"
#include "build_style/build_style.h"
#include "build_style/build_statistics.h"
#include "build_style/run_tests.h"

#include "drape_frontend/debug_rect_renderer.hpp"
#endif // BUILD_DESIGNER

#include <QtGui/QCloseEvent>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>

#ifdef OMIM_OS_WINDOWS
#define IDM_ABOUT_DIALOG        1001
#define IDM_PREFERENCES_DIALOG  1002
#endif

#ifndef NO_DOWNLOADER
#include "qt/update_dialog.hpp"
#include "qt/info_dialog.hpp"
#endif // NO_DOWNLOADER

namespace qt
{
namespace
{
void FormatMapSize(uint64_t sizeInBytes, std::string & units, size_t & sizeToDownload)
{
  int const mbInBytes = 1024 * 1024;
  int const kbInBytes = 1024;
  if (sizeInBytes > mbInBytes)
  {
    sizeToDownload = (sizeInBytes + mbInBytes - 1) / mbInBytes;
    units = "MB";
  }
  else if (sizeInBytes > kbInBytes)
  {
    sizeToDownload = (sizeInBytes + kbInBytes -1) / kbInBytes;
    units = "KB";
  }
  else
  {
    sizeToDownload = sizeInBytes;
    units = "B";
  }
}

template <class T> T * CreateBlackControl(QString const & name)
{
  T * p = new T(name);
  p->setStyleSheet("color: black;");
  return p;
}

}  // namespace

// Defined in osm_auth_dialog.cpp.
extern char const * kOauthTokenSetting;

MainWindow::MainWindow(Framework & framework,
                       std::unique_ptr<ScreenshotParams> && screenshotParams,
                       QRect const & screenGeometry
#ifdef BUILD_DESIGNER
                       , QString const & mapcssFilePath
#endif
                       )
  : m_locationService(CreateDesktopLocationService(*this))
  , m_screenshotMode(screenshotParams != nullptr)
#ifdef BUILD_DESIGNER
  , m_mapcssFilePath(mapcssFilePath)
#endif
{
  setGeometry(screenGeometry);

  if (m_screenshotMode)
  {
    screenshotParams->m_statusChangedFn = [this](std::string const & state, bool finished)
    {
      statusBar()->showMessage(QString::fromStdString(state));
      if (finished)
        QCoreApplication::quit();
    };
  }

  int const width = m_screenshotMode ? static_cast<int>(screenshotParams->m_width) : 0;
  int const height = m_screenshotMode ? static_cast<int>(screenshotParams->m_height) : 0;

  m_VulkanInstance = new QVulkanInstance;
#ifdef ENABLE_VULKAN_DIAGNOSTICS
  m_VulkanInstance->setLayers({ "VK_LAYER_KHRONOS_validation" }); // This is also set in drape/vulkan/vulkan_layers.cpp
#endif
  if (!m_VulkanInstance->create())
  {
    LOG(LINFO, ("Failed to create Vulkan instance, Vulkan is likely not supported: ", m_VulkanInstance->errorCode()));
  }
  else
  {
    LOG(LINFO, ("Successfully create Vulkan instance"));
    m_window = new qt::VulkanWindow;
    m_window->setVulkanInstance(m_VulkanInstance);
    m_Wrapper = QWidget::createWindowContainer(m_window, this);
    setCentralWidget(m_Wrapper);
  }

#if 0
  m_pDrawWidget = new DrawWidget(framework, std::move(screenshotParams), this);

  QList<Qt::GestureType> gestures;
  gestures << Qt::PinchGesture;
  m_pDrawWidget->grabGestures(gestures);

  setCentralWidget(m_pDrawWidget);

  if (m_screenshotMode)
  {
    m_pDrawWidget->setFixedSize(width, height);
    setFixedSize(width, height + statusBar()->height());
  }

  connect(m_pDrawWidget, SIGNAL(BeforeEngineCreation()), this, SLOT(OnBeforeEngineCreation()));
  CreateCountryStatusControls();
#endif
  CreateNavigationBar();
  CreateSearchBarAndPanel();

  QString caption = QCoreApplication::applicationName();

#ifdef BUILD_DESIGNER
  if (!m_mapcssFilePath.isEmpty())
    caption += QString(" - ") + m_mapcssFilePath;
#endif

  setWindowTitle(caption);
  setWindowIcon(QIcon(":/ui/logo.png"));

#ifndef OMIM_OS_WINDOWS
  QMenu * helpMenu = new QMenu(tr("Help"), this);
  menuBar()->addMenu(helpMenu);
  helpMenu->addAction(tr("OpenStreetMap Login"), this, SLOT(OnLoginMenuItem()), QKeySequence(Qt::CTRL | Qt::Key_O));
  helpMenu->addAction(tr("Upload Edits"), this, SLOT(OnUploadEditsMenuItem()), QKeySequence(Qt::CTRL | Qt::Key_U));
  helpMenu->addAction(tr("Preferences"), this, SLOT(OnPreferences()), QKeySequence(Qt::CTRL | Qt::Key_P));
  helpMenu->addAction(tr("About"), this, SLOT(OnAbout()), QKeySequence(Qt::Key_F1));
  helpMenu->addAction(tr("Exit"), this, SLOT(close()), QKeySequence(Qt::CTRL | Qt::Key_Q));
#else
  {
    // create items in the system menu
    HMENU menu = ::GetSystemMenu((HWND)winId(), FALSE);
    MENUITEMINFOA item;
    item.cbSize = sizeof(MENUITEMINFOA);
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
    item.fType = MFT_STRING;
    item.wID = IDM_PREFERENCES_DIALOG;
    QByteArray const prefsStr = tr("Preferences...").toLocal8Bit();
    item.dwTypeData = const_cast<char *>(prefsStr.data());
    item.cch = prefsStr.size();
    ::InsertMenuItemA(menu, ::GetMenuItemCount(menu) - 1, TRUE, &item);
    item.wID = IDM_ABOUT_DIALOG;
    QByteArray const aboutStr = tr("About...").toLocal8Bit();
    item.dwTypeData = const_cast<char *>(aboutStr.data());
    item.cch = aboutStr.size();
    ::InsertMenuItemA(menu, ::GetMenuItemCount(menu) - 1, TRUE, &item);
    item.fType = MFT_SEPARATOR;
    ::InsertMenuItemA(menu, ::GetMenuItemCount(menu) - 1, TRUE, &item);
  }
#endif

  // Always show on full screen.
  showMaximized();

#ifndef NO_DOWNLOADER
  // Show intro dialog if necessary
  bool bShow = true;
  std::string const showWelcome = "ShowWelcome";
  settings::TryGet(showWelcome, bShow);

  if (bShow)
  {
    bool bShowUpdateDialog = true;

    std::string text;
    try
    {
      ReaderPtr<Reader> reader = GetPlatform().GetReader("welcome.html");
      reader.ReadAsString(text);
    }
    catch (...)
    {}

    if (!text.empty())
    {
      InfoDialog welcomeDlg(QString("Welcome to ") + caption, text.c_str(),
                            this, QStringList(tr("Download Maps")));
      if (welcomeDlg.exec() == QDialog::Rejected)
        bShowUpdateDialog = false;
    }
    settings::Set("ShowWelcome", false);

    if (bShowUpdateDialog)
      ShowUpdateDialog();
  }
#endif // NO_DOWNLOADER

  //m_pDrawWidget->UpdateAfterSettingsChanged();

  //RoutingSettings::LoadSession(m_pDrawWidget->GetFramework());
}

#if defined(OMIM_OS_WINDOWS)
bool MainWindow::nativeEvent(QByteArray const & eventType, void * message, qintptr * result)
{
  MSG * msg = static_cast<MSG *>(message);
  if (msg->message == WM_SYSCOMMAND)
  {
    switch (msg->wParam)
    {
    case IDM_PREFERENCES_DIALOG:
      OnPreferences();
      *result = 0;
      return true;
    case IDM_ABOUT_DIALOG:
      OnAbout();
      *result = 0;
      return true;
    }
  }
  return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::LocationStateModeChanged(location::EMyPositionMode mode)
{
  if (mode == location::PendingPosition)
  {
    m_locationService->Start();
    m_pMyPositionAction->setIcon(QIcon(":/navig64/location-search.png"));
    m_pMyPositionAction->setToolTip(tr("Looking for position..."));
    return;
  }

  m_pMyPositionAction->setIcon(QIcon(":/navig64/location.png"));
  m_pMyPositionAction->setToolTip(tr("My Position"));
}

void MainWindow::CreateNavigationBar()
{
  QToolBar * pToolBar = new QToolBar(tr("Navigation Bar"), this);
  pToolBar->setOrientation(Qt::Vertical);
  pToolBar->setIconSize(QSize(32, 32));
  {
    //m_pDrawWidget->BindHotkeys(*this);

    // Add navigation hot keys.
    qt::common::Hotkey const hotkeys[] = {
      { Qt::Key_A, SLOT(ShowAll()) },
      // Use CMD+n (New Item hotkey) to activate Create Feature mode.
      { Qt::Key_Escape, SLOT(ChoosePositionModeDisable()) }
    };

    for (auto const & hotkey : hotkeys)
    {
      QAction * pAct = new QAction(this);
      pAct->setShortcut(QKeySequence(hotkey.m_key));
      //connect(pAct, SIGNAL(triggered()), m_pDrawWidget, hotkey.m_slot);
      addAction(pAct);
    }
  }

  {
    using namespace std::placeholders;

    m_layers = new PopupMenuHolder(this);

    m_layers->addAction(QIcon(":/navig64/traffic.png"), tr("Traffic"),
                        std::bind(&MainWindow::OnLayerEnabled, this, LayerType::TRAFFIC), true);
//    m_layers->setChecked(LayerType::TRAFFIC, m_pDrawWidget->GetFramework().LoadTrafficEnabled());

    m_layers->addAction(QIcon(":/navig64/subway.png"), tr("Public transport"),
                        std::bind(&MainWindow::OnLayerEnabled, this, LayerType::TRANSIT), true);
//    m_layers->setChecked(LayerType::TRANSIT, m_pDrawWidget->GetFramework().LoadTransitSchemeEnabled());

    m_layers->addAction(QIcon(":/navig64/isolines.png"), tr("Isolines"),
                        std::bind(&MainWindow::OnLayerEnabled, this, LayerType::ISOLINES), true);
//    m_layers->setChecked(LayerType::ISOLINES, m_pDrawWidget->GetFramework().LoadIsolinesEnabled());

    pToolBar->addWidget(m_layers->create());
    m_layers->setMainIcon(QIcon(":/navig64/layers.png"));

    pToolBar->addSeparator();

    pToolBar->addAction(QIcon(":/navig64/bookmark.png"), tr("Show bookmarks and tracks; use ALT + RMB to add a bookmark"),
                        this, SLOT(OnBookmarksAction()));
    pToolBar->addSeparator();

#ifndef BUILD_DESIGNER
    m_routing = new PopupMenuHolder(this);

    // The order should be the same as in "enum class RouteMarkType".
    m_routing->addAction(QIcon(":/navig64/point-start.png"), tr("Start point"),
                         std::bind(&MainWindow::OnRoutePointSelected, this, RouteMarkType::Start), false);
    m_routing->addAction(QIcon(":/navig64/point-intermediate.png"), tr("Intermediate point"),
                         std::bind(&MainWindow::OnRoutePointSelected, this, RouteMarkType::Intermediate), false);
    m_routing->addAction(QIcon(":/navig64/point-finish.png"), tr("Finish point"),
                         std::bind(&MainWindow::OnRoutePointSelected, this, RouteMarkType::Finish), false);

    QToolButton * toolBtn = m_routing->create();
    toolBtn->setToolTip(tr("Select mode and use SHIFT + LMB to set point"));
    pToolBar->addWidget(toolBtn);
//    m_routing->setCurrent(m_pDrawWidget->GetRoutePointAddMode());

    QAction * act = pToolBar->addAction(QIcon(":/navig64/routing.png"), tr("Follow route"), this, SLOT(OnFollowRoute()));
    act->setToolTip(tr("Build route and use ALT + LMB to emulate current position"));
    pToolBar->addAction(QIcon(":/navig64/clear-route.png"), tr("Clear route"), this, SLOT(OnClearRoute()));
    pToolBar->addAction(QIcon(":/navig64/settings-routing.png"), tr("Routing settings"), this, SLOT(OnRoutingSettings()));

    pToolBar->addSeparator();

    m_pCreateFeatureAction = pToolBar->addAction(QIcon(":/navig64/select.png"), tr("Create Feature"),
                                                 this, SLOT(OnCreateFeatureClicked()));
    m_pCreateFeatureAction->setCheckable(true);
    m_pCreateFeatureAction->setToolTip(tr("Push to select position, next push to create Feature"));
    m_pCreateFeatureAction->setShortcut(QKeySequence::New);

    pToolBar->addSeparator();

    m_selection = new PopupMenuHolder(this);

    // The order should be the same as in "enum class SelectionMode".
    m_selection->addAction(QIcon(":/navig64/selectmode.png"), tr("Roads selection mode"),
                           std::bind(&MainWindow::OnSwitchSelectionMode, this, SelectionMode::Features), true);
    m_selection->addAction(QIcon(":/navig64/city_boundaries.png"), tr("City boundaries selection mode"),
                           std::bind(&MainWindow::OnSwitchSelectionMode, this, SelectionMode::CityBoundaries), true);
    m_selection->addAction(QIcon(":/navig64/city_roads.png"), tr("City roads selection mode"),
                           std::bind(&MainWindow::OnSwitchSelectionMode, this, SelectionMode::CityRoads), true);
    m_selection->addAction(QIcon(":/navig64/test.png"), tr("Cross MWM segments selection mode"),
                           std::bind(&MainWindow::OnSwitchSelectionMode, this, SelectionMode::CrossMwmSegments), true);
    m_selection->addAction(QIcon(":/navig64/borders_selection.png"), tr("MWMs borders selection mode"),
                           this, SLOT(OnSwitchMwmsBordersSelectionMode()), true);

    toolBtn = m_selection->create();
    toolBtn->setToolTip(tr("Select mode and use RMB to define selection box"));
    pToolBar->addWidget(toolBtn);

    pToolBar->addAction(QIcon(":/navig64/clear.png"), tr("Clear selection"), this, SLOT(OnClearSelection()));

    pToolBar->addSeparator();

#endif // NOT BUILD_DESIGNER

    // Add search button with "checked" behavior.
    m_pSearchAction = pToolBar->addAction(QIcon(":/navig64/search.png"), tr("Offline Search"),
                                          this, SLOT(OnSearchButtonClicked()));
    m_pSearchAction->setCheckable(true);
    m_pSearchAction->setShortcut(QKeySequence::Find);

    m_rulerAction = pToolBar->addAction(QIcon(":/navig64/ruler.png"), tr("Ruler"), this, SLOT(OnRulerEnabled()));
    m_rulerAction->setToolTip(tr("Check this button and use ALT + LMB to set points"));
    m_rulerAction->setCheckable(true);
    m_rulerAction->setChecked(false);

    pToolBar->addSeparator();

    // add my position button with "checked" behavior

    m_pMyPositionAction = pToolBar->addAction(QIcon(":/navig64/location.png"), tr("My Position"), this, SLOT(OnMyPosition()));
    m_pMyPositionAction->setCheckable(true);

#ifdef BUILD_DESIGNER
    // Add "Build style" button
    if (!m_mapcssFilePath.isEmpty())
    {
      m_pBuildStyleAction = pToolBar->addAction(QIcon(":/navig64/run.png"),
                                                tr("Build style"),
                                                this,
                                                SLOT(OnBuildStyle()));
      m_pBuildStyleAction->setCheckable(false);
      m_pBuildStyleAction->setToolTip(tr("Build style"));

      m_pRecalculateGeomIndex = pToolBar->addAction(QIcon(":/navig64/geom.png"),
                                                    tr("Recalculate geometry index"),
                                                    this,
                                                    SLOT(OnRecalculateGeomIndex()));
      m_pRecalculateGeomIndex->setCheckable(false);
      m_pRecalculateGeomIndex->setToolTip(tr("Recalculate geometry index"));
    }

    // Add "Debug style" button
    m_pDrawDebugRectAction = pToolBar->addAction(QIcon(":/navig64/bug.png"),
                                              tr("Debug style"),
                                              this,
                                              SLOT(OnDebugStyle()));
    m_pDrawDebugRectAction->setCheckable(true);
    m_pDrawDebugRectAction->setChecked(false);
    m_pDrawDebugRectAction->setToolTip(tr("Debug style"));
//    m_pDrawWidget->GetFramework().EnableDebugRectRendering(false);

    // Add "Get statistics" button
    m_pGetStatisticsAction = pToolBar->addAction(QIcon(":/navig64/chart.png"),
                                                 tr("Get statistics"),
                                                 this,
                                                 SLOT(OnGetStatistics()));
    m_pGetStatisticsAction->setCheckable(false);
    m_pGetStatisticsAction->setToolTip(tr("Get statistics"));

    // Add "Run tests" button
    m_pRunTestsAction = pToolBar->addAction(QIcon(":/navig64/test.png"),
                                            tr("Run tests"),
                                            this,
                                            SLOT(OnRunTests()));
    m_pRunTestsAction->setCheckable(false);
    m_pRunTestsAction->setToolTip(tr("Run tests"));

    // Add "Build phone package" button
    m_pBuildPhonePackAction = pToolBar->addAction(QIcon(":/navig64/phonepack.png"),
                                                  tr("Build phone package"),
                                                  this,
                                                  SLOT(OnBuildPhonePackage()));
    m_pBuildPhonePackAction->setCheckable(false);
    m_pBuildPhonePackAction->setToolTip(tr("Build phone package"));
#endif // BUILD_DESIGNER
  }

  pToolBar->addSeparator();
//  qt::common::ScaleSlider::Embed(Qt::Vertical, *pToolBar, *m_pDrawWidget);

#ifndef NO_DOWNLOADER
  pToolBar->addSeparator();
  pToolBar->addAction(QIcon(":/navig64/download.png"), tr("Download Maps"), this, SLOT(ShowUpdateDialog()));
#endif // NO_DOWNLOADER

  if (m_screenshotMode)
    pToolBar->setVisible(false);

  addToolBar(Qt::RightToolBarArea, pToolBar);
}

Framework & MainWindow::GetFramework() const
{
  return m_pDrawWidget->GetFramework();
}

void MainWindow::CreateCountryStatusControls()
{
  QHBoxLayout * mainLayout = new QHBoxLayout();
  m_downloadButton = CreateBlackControl<QPushButton>("Download");
  mainLayout->addWidget(m_downloadButton, 0, Qt::AlignHCenter);
  m_downloadButton->setVisible(false);
  connect(m_downloadButton, &QAbstractButton::released, this, &MainWindow::OnDownloadClicked);

  m_retryButton = CreateBlackControl<QPushButton>("Retry downloading");
  mainLayout->addWidget(m_retryButton, 0, Qt::AlignHCenter);
  m_retryButton->setVisible(false);
  connect(m_retryButton, &QAbstractButton::released, this, &MainWindow::OnRetryDownloadClicked);

  m_downloadingStatusLabel = CreateBlackControl<QLabel>("Downloading");
  mainLayout->addWidget(m_downloadingStatusLabel, 0, Qt::AlignHCenter);
  m_downloadingStatusLabel->setVisible(false);

  //m_pDrawWidget->setLayout(mainLayout);

  auto const OnCountryChanged = [this](storage::CountryId const & countryId)
  {
    m_downloadButton->setVisible(false);
    m_retryButton->setVisible(false);
    m_downloadingStatusLabel->setVisible(false);

    m_lastCountry = countryId;
    // Called by Framework in World zoom level.
    if (countryId.empty())
      return;

    auto const & storage = GetFramework().GetStorage();
    auto status = storage.CountryStatusEx(countryId);
    auto const & countryName = countryId;

    if (status == storage::Status::NotDownloaded)
    {
      m_downloadButton->setVisible(true);

      std::string units;
      size_t sizeToDownload = 0;
      FormatMapSize(storage.CountrySizeInBytes(countryId).second, units, sizeToDownload);
      std::stringstream str;
      str << "Download (" << countryName << ") " << sizeToDownload << units;
      m_downloadButton->setText(str.str().c_str());
    }
    else if (status == storage::Status::Downloading)
    {
      m_downloadingStatusLabel->setVisible(true);
    }
    else if (status == storage::Status::InQueue)
    {
      m_downloadingStatusLabel->setVisible(true);

      std::stringstream str;
      str << countryName << " is waiting for downloading";
      m_downloadingStatusLabel->setText(str.str().c_str());
    }
    else if (status != storage::Status::OnDisk && status != storage::Status::OnDiskOutOfDate)
    {
      m_retryButton->setVisible(true);

      std::stringstream str;
      str << "Retry to download " << countryName;
      m_retryButton->setText(str.str().c_str());
    }
  };

  GetFramework().SetCurrentCountryChangedListener(OnCountryChanged);

  GetFramework().GetStorage().Subscribe(
    [this, onChanged = std::move(OnCountryChanged)](storage::CountryId const & countryId)
    {
      // Storage also calls notifications for parents, but we are interested in leafs only.
      if (GetFramework().GetStorage().IsLeaf(countryId))
        onChanged(countryId);
    },
    [this](storage::CountryId const & countryId, downloader::Progress const & progress)
    {
      std::stringstream str;
      str << "Downloading (" << countryId << ") " << progress.m_bytesDownloaded * 100 / progress.m_bytesTotal << "%";
      m_downloadingStatusLabel->setText(str.str().c_str());
    });
}

void MainWindow::OnAbout()
{
  AboutDialog dlg(this);
  dlg.exec();
}

void MainWindow::OnLocationError(location::TLocationError errorCode)
{
  switch (errorCode)
  {
  case location::EDenied:  [[fallthrough]];
  case location::ETimeout: [[fallthrough]];
  case location::EUnknown:
    {
      //if (m_pDrawWidget && m_pMyPositionAction)
      //  m_pMyPositionAction->setEnabled(false);
      break;
    }

  default:
    ASSERT(false, ("Not handled location notification:", errorCode));
    break;
  }

  //if (m_pDrawWidget != nullptr)
  //m_pDrawWidget->GetFramework().OnLocationError(errorCode);
}

void MainWindow::OnLocationUpdated(location::GpsInfo const & info)
{
  //m_pDrawWidget->GetFramework().OnLocationUpdate(info);
}

void MainWindow::OnMyPosition()
{
//  if (m_pMyPositionAction->isEnabled())
//    m_pDrawWidget->GetFramework().SwitchMyPositionNextMode();
}

void MainWindow::OnCreateFeatureClicked()
{
//  if (m_pCreateFeatureAction->isChecked())
//  {
//    m_pDrawWidget->ChoosePositionModeEnable();
//  }
//  else
//  {
//    m_pDrawWidget->ChoosePositionModeDisable();
//    m_pDrawWidget->CreateFeature();
//  }
}

void MainWindow::OnSwitchSelectionMode(SelectionMode mode)
{
//  if (m_selection->isChecked(mode))
//  {
//    m_selection->setCurrent(mode);
//    m_pDrawWidget->SetSelectionMode(mode);
//  }
//  else
//    OnClearSelection();
}

void MainWindow::OnSwitchMwmsBordersSelectionMode()
{
  MwmsBordersSelection dlg(this);
  auto const response = dlg.ShowModal();
//  if (response == SelectionMode::Cancelled)
//  {
//    m_pDrawWidget->DropSelectionIfMWMBordersMode();
//    return;
//  }
//
//  m_selection->setCurrent(SelectionMode::MWMBorders);
//  m_pDrawWidget->SetSelectionMode(response);
}

void MainWindow::OnClearSelection()
{
//  m_pDrawWidget->GetFramework().GetDrapeApi().Clear();
//  m_pDrawWidget->SetSelectionMode({});

  m_selection->setMainIcon({});
}

void MainWindow::OnSearchButtonClicked()
{
  if (m_pSearchAction->isChecked())
    m_Docks[0]->show();
  else
    m_Docks[0]->hide();
}

void MainWindow::OnLoginMenuItem()
{
  OsmAuthDialog dlg(this);
  dlg.exec();
}

void MainWindow::OnUploadEditsMenuItem()
{
  std::string token;
  if (!settings::Get(kOauthTokenSetting, token) || token.empty())
  {
    OnLoginMenuItem();
  }
  else
  {
    auto & editor = osm::Editor::Instance();
    if (editor.HaveMapEditsOrNotesToUpload())
      editor.UploadChanges(token, {{"created_by", "Organic Maps " OMIM_OS_NAME}});
  }
}

void MainWindow::OnBeforeEngineCreation()
{
  // m_pDrawWidget->GetFramework().SetMyPositionModeListener([this](location::EMyPositionMode mode, bool /*routingActive*/)
  // {
  //   LocationStateModeChanged(mode);
  // });
}

void MainWindow::OnPreferences()
{
//  Framework & framework = m_pDrawWidget->GetFramework();
//  PreferencesDialog dlg(this, framework);
//  dlg.exec();
//
//  framework.EnterForeground();
}

#ifdef BUILD_DESIGNER
void MainWindow::OnBuildStyle()
{
  try
  {
    build_style::BuildAndApply(m_mapcssFilePath);
    m_pDrawWidget->RefreshDrawingRules();

    bool enabled;
    if (!settings::Get(kEnabledAutoRegenGeomIndex, enabled))
      enabled = false;

    if (enabled)
    {
      build_style::NeedRecalculate = true;
      QMainWindow::close();
    }
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}

void MainWindow::OnRecalculateGeomIndex()
{
  try
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("Geometry index will be regenerated. It can take a while.\nApplication may be closed and reopened!");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    if (msgBox.exec() == QMessageBox::Yes)
    {
      build_style::NeedRecalculate = true;
      QMainWindow::close();
    }
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}

void MainWindow::OnDebugStyle()
{
//  bool const checked = m_pDrawDebugRectAction->isChecked();
//  m_pDrawWidget->GetFramework().EnableDebugRectRendering(checked);
//  m_pDrawWidget->RefreshDrawingRules();
}

void MainWindow::OnGetStatistics()
{
  try
  {
    QString text = build_style::GetCurrentStyleStatistics();
    InfoDialog dlg(QString("Style statistics"), text, NULL);
    dlg.exec();
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}

void MainWindow::OnRunTests()
{
  try
  {
    std::pair<bool, QString> res = build_style::RunCurrentStyleTests();
    InfoDialog dlg(QString("Style tests: ") + (res.first ? "OK" : "FAILED"), res.second, NULL);
    dlg.exec();
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}

void MainWindow::OnBuildPhonePackage()
{
  try
  {
    char const * const kStylesFolder = "styles";
    char const * const kClearStyleFolder = "clear";

    QString const targetDir = QFileDialog::getExistingDirectory(nullptr, "Choose output directory");
    if (targetDir.isEmpty())
      return;
    auto outDir = QDir(JoinPathQt({targetDir, kStylesFolder}));
    if (outDir.exists())
    {
      QMessageBox msgBox;
      msgBox.setWindowTitle("Warning");
      msgBox.setText(QString("Folder ") + outDir.absolutePath() + " will be deleted?");
      msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      msgBox.setDefaultButton(QMessageBox::No);
      auto result = msgBox.exec();
      if (result == QMessageBox::No)
        throw std::runtime_error(std::string("Target directory exists: ") + outDir.absolutePath().toStdString());
    }

    QString const stylesDir = JoinPathQt({m_mapcssFilePath, "..", "..", ".."});
    if (!QDir(JoinPathQt({stylesDir, kClearStyleFolder})).exists())
      throw std::runtime_error(std::string("Styles folder is not found in ") + stylesDir.toStdString());

    QString text = build_style::RunBuildingPhonePack(stylesDir, targetDir);
    text.append("\nMobile device style package is in the directory: ");
    text.append(JoinPathQt({targetDir, kStylesFolder}));
    text.append(". Copy it to your mobile device.\n");
    InfoDialog dlg(QString("Building phone pack"), text, nullptr);
    dlg.exec();
  }
  catch (std::exception & e)
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Error");
    msgBox.setText(e.what());
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
  }
}
#endif // BUILD_DESIGNER

#ifndef NO_DOWNLOADER
void MainWindow::ShowUpdateDialog()
{
//  UpdateDialog dlg(this, m_pDrawWidget->GetFramework());
//  dlg.ShowModal();
//  m_pDrawWidget->update();
}

#endif // NO_DOWNLOADER

void MainWindow::CreateSearchBarAndPanel()
{
  CreatePanelImpl(0, Qt::RightDockWidgetArea, tr("Search"), QKeySequence(), 0);

//  SearchPanel * panel = new SearchPanel(m_pDrawWidget, m_Docks[0]);
//  m_Docks[0]->setWidget(panel);
}

void MainWindow::CreatePanelImpl(size_t i, Qt::DockWidgetArea area, QString const & name,
                                 QKeySequence const & hotkey, char const * slot)
{
  ASSERT_LESS(i, m_Docks.size(), ());
  m_Docks[i] = new QDockWidget(name, this);

  addDockWidget(area, m_Docks[i]);

  // hide by default
  m_Docks[i]->hide();

  // register a hotkey to show panel
  if (slot && !hotkey.isEmpty())
  {
    QAction * pAct = new QAction(this);
    pAct->setShortcut(hotkey);
    connect(pAct, SIGNAL(triggered()), this, slot);
    addAction(pAct);
  }
}

void MainWindow::closeEvent(QCloseEvent * e)
{
 // m_pDrawWidget->PrepareShutdown();
  e->accept();
}

void MainWindow::OnDownloadClicked()
{
  GetFramework().GetStorage().DownloadNode(m_lastCountry);
}

void MainWindow::OnRetryDownloadClicked()
{
  GetFramework().GetStorage().RetryDownloadNode(m_lastCountry);
}

void MainWindow::SetLayerEnabled(LayerType type, bool enable)
{
//  auto & frm = m_pDrawWidget->GetFramework();
//  switch (type)
//  {
//  case LayerType::TRAFFIC:
//  /// @todo Uncomment when we will integrate a traffic provider.
//  // frm.GetTrafficManager().SetEnabled(enable);
//  // frm.SaveTrafficEnabled(enable);
//    break;
//  case LayerType::TRANSIT:
//    frm.GetTransitManager().EnableTransitSchemeMode(enable);
//    frm.SaveTransitSchemeEnabled(enable);
//    break;
//  case LayerType::ISOLINES:
//    frm.GetIsolinesManager().SetEnabled(enable);
//    frm.SaveIsolinesEnabled(enable);
//    break;
//  default:
//    UNREACHABLE();
//    break;
//  }
}

void MainWindow::OnLayerEnabled(LayerType layer)
{
  for (size_t i = 0; i < LayerType::COUNT; ++i)
  {
    if (i == layer)
      SetLayerEnabled(static_cast<LayerType>(i), m_layers->isChecked(i));
    else
    {
      m_layers->setChecked(i, false);
      SetLayerEnabled(static_cast<LayerType>(i), false);
    }
  }
}

void MainWindow::OnRulerEnabled()
{
//  m_pDrawWidget->SetRuler(m_rulerAction->isChecked());
}

void MainWindow::OnRoutePointSelected(RouteMarkType type)
{
//  m_routing->setCurrent(type);
//  m_pDrawWidget->SetRoutePointAddMode(type);
}

void MainWindow::OnFollowRoute()
{
//  m_pDrawWidget->FollowRoute();
}

void MainWindow::OnClearRoute()
{
//  m_pDrawWidget->ClearRoute();
}

void MainWindow::OnRoutingSettings()
{
//  RoutingSettings dlg(this, m_pDrawWidget->GetFramework());
//  dlg.ShowModal();
}

void MainWindow::OnBookmarksAction()
{
//  BookmarkDialog dlg(this, m_pDrawWidget->GetFramework());
//  dlg.ShowModal();
//  m_pDrawWidget->update();
}

#if defined(OMIM_OS_LINUX)
// Note that the vertex data and the projection matrix assume OpenGL. With
// Vulkan Y is negated in clip space and the near/far plane is at 0/1 instead
// of -1/1. These will be corrected for by an extra transformation when
// calculating the modelview-projection matrix.
static float vertexData[] = { // Y up, front = CCW
     0.0f,   0.5f,   1.0f, 0.0f, 0.0f,
    -0.5f,  -0.5f,   0.0f, 1.0f, 0.0f,
     0.5f,  -0.5f,   0.0f, 0.0f, 1.0f
};

static const int UNIFORM_DATA_SIZE = 16 * sizeof(float);

static inline VkDeviceSize aligned(VkDeviceSize v, VkDeviceSize byteAlign)
{
    return (v + byteAlign - 1) & ~(byteAlign - 1);
}

TriangleRenderer::TriangleRenderer(QVulkanWindow *w, bool msaa)
    : m_window(w)
{
    if (msaa) {
        const QList<int> counts = w->supportedSampleCounts();
        qDebug() << "Supported sample counts:" << counts;
        for (int s = 16; s >= 4; s /= 2) {
            if (counts.contains(s)) {
                qDebug("Requesting sample count %d", s);
                m_window->setSampleCount(s);
                break;
            }
        }
    }
}

VkShaderModule TriangleRenderer::createShader(const QString &name)
{
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Failed to read shader %s", qPrintable(name));
        return VK_NULL_HANDLE;
    }
    QByteArray blob = file.readAll();
    file.close();

    VkShaderModuleCreateInfo shaderInfo;
    memset(&shaderInfo, 0, sizeof(shaderInfo));
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = blob.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t *>(blob.constData());
    VkShaderModule shaderModule;
    VkResult err = m_devFuncs->vkCreateShaderModule(m_window->device(), &shaderInfo, nullptr, &shaderModule);
    if (err != VK_SUCCESS) {
        qWarning("Failed to create shader module: %d", err);
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

void TriangleRenderer::initResources()
{
    qDebug("initResources");

    VkDevice dev = m_window->device();
    m_devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);

    // Prepare the vertex and uniform data. The vertex data will never
    // change so one buffer is sufficient regardless of the value of
    // QVulkanWindow::CONCURRENT_FRAME_COUNT. Uniform data is changing per
    // frame however so active frames have to have a dedicated copy.

    // Use just one memory allocation and one buffer. We will then specify the
    // appropriate offsets for uniform buffers in the VkDescriptorBufferInfo.
    // Have to watch out for
    // VkPhysicalDeviceLimits::minUniformBufferOffsetAlignment, though.

    // The uniform buffer is not strictly required in this example, we could
    // have used push constants as well since our single matrix (64 bytes) fits
    // into the spec mandated minimum limit of 128 bytes. However, once that
    // limit is not sufficient, the per-frame buffers, as shown below, will
    // become necessary.

    const int concurrentFrameCount = m_window->concurrentFrameCount();
    const VkPhysicalDeviceLimits *pdevLimits = &m_window->physicalDeviceProperties()->limits;
    const VkDeviceSize uniAlign = pdevLimits->minUniformBufferOffsetAlignment;
    qDebug("uniform buffer offset alignment is %u", (uint) uniAlign);
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    // Our internal layout is vertex, uniform, uniform, ... with each uniform buffer start offset aligned to uniAlign.
    const VkDeviceSize vertexAllocSize = aligned(sizeof(vertexData), uniAlign);
    const VkDeviceSize uniformAllocSize = aligned(UNIFORM_DATA_SIZE, uniAlign);
    bufInfo.size = vertexAllocSize + concurrentFrameCount * uniformAllocSize;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VkResult err = m_devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &m_buf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create buffer: %d", err);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(dev, m_buf, &memReq);

    VkMemoryAllocateInfo memAllocInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        memReq.size,
        m_window->hostVisibleMemoryIndex()
    };

    err = m_devFuncs->vkAllocateMemory(dev, &memAllocInfo, nullptr, &m_bufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(dev, m_buf, m_bufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind buffer memory: %d", err);

    quint8 *p;
    err = m_devFuncs->vkMapMemory(dev, m_bufMem, 0, memReq.size, 0, reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, vertexData, sizeof(vertexData));
    QMatrix4x4 ident;
    memset(m_uniformBufInfo, 0, sizeof(m_uniformBufInfo));
    for (int i = 0; i < concurrentFrameCount; ++i) {
        const VkDeviceSize offset = vertexAllocSize + i * uniformAllocSize;
        memcpy(p + offset, ident.constData(), 16 * sizeof(float));
        m_uniformBufInfo[i].buffer = m_buf;
        m_uniformBufInfo[i].offset = offset;
        m_uniformBufInfo[i].range = uniformAllocSize;
    }
    m_devFuncs->vkUnmapMemory(dev, m_bufMem);

    VkVertexInputBindingDescription vertexBindingDesc = {
        0, // binding
        5 * sizeof(float),
        VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription vertexAttrDesc[] = {
        { // position
            0, // location
            0, // binding
            VK_FORMAT_R32G32_SFLOAT,
            0
        },
        { // color
            1,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            2 * sizeof(float)
        }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    // Set up descriptor set and its layout.
    VkDescriptorPoolSize descPoolSizes = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uint32_t(concurrentFrameCount) };
    VkDescriptorPoolCreateInfo descPoolInfo;
    memset(&descPoolInfo, 0, sizeof(descPoolInfo));
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = concurrentFrameCount;
    descPoolInfo.poolSizeCount = 1;
    descPoolInfo.pPoolSizes = &descPoolSizes;
    err = m_devFuncs->vkCreateDescriptorPool(dev, &descPoolInfo, nullptr, &m_descPool);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor pool: %d", err);

    VkDescriptorSetLayoutBinding layoutBinding = {
        0, // binding
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_VERTEX_BIT,
        nullptr
    };
    VkDescriptorSetLayoutCreateInfo descLayoutInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1,
        &layoutBinding
    };
    err = m_devFuncs->vkCreateDescriptorSetLayout(dev, &descLayoutInfo, nullptr, &m_descSetLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor set layout: %d", err);

    for (int i = 0; i < concurrentFrameCount; ++i) {
        VkDescriptorSetAllocateInfo descSetAllocInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            nullptr,
            m_descPool,
            1,
            &m_descSetLayout
        };
        err = m_devFuncs->vkAllocateDescriptorSets(dev, &descSetAllocInfo, &m_descSet[i]);
        if (err != VK_SUCCESS)
            qFatal("Failed to allocate descriptor set: %d", err);

        VkWriteDescriptorSet descWrite;
        memset(&descWrite, 0, sizeof(descWrite));
        descWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descWrite.dstSet = m_descSet[i];
        descWrite.descriptorCount = 1;
        descWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descWrite.pBufferInfo = &m_uniformBufInfo[i];
        m_devFuncs->vkUpdateDescriptorSets(dev, 1, &descWrite, 0, nullptr);
    }

    // Pipeline cache
    VkPipelineCacheCreateInfo pipelineCacheInfo;
    memset(&pipelineCacheInfo, 0, sizeof(pipelineCacheInfo));
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    err = m_devFuncs->vkCreatePipelineCache(dev, &pipelineCacheInfo, nullptr, &m_pipelineCache);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline cache: %d", err);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descSetLayout;
    err = m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    // Shaders
    VkShaderModule vertShaderModule = createShader("./color_vert.spv");//QStringLiteral(":/color_vert.spv"));
    VkShaderModule fragShaderModule = createShader("./color_frag.spv");//QStringLiteral(":/color_frag.spv"));

    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo;
    memset(&pipelineInfo, 0, sizeof(pipelineInfo));
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            vertShaderModule,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            fragShaderModule,
            "main",
            nullptr
        }
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.pInputAssemblyState = &ia;

    // The viewport and scissor will be set dynamically via vkCmdSetViewport/Scissor.
    // This way the pipeline does not need to be touched when resizing the window.
    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    pipelineInfo.pViewportState = &vp;

    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // we want the back face as well
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rs;

    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    // Enable multisampling.
    ms.rasterizationSamples = m_window->sampleCountFlagBits();
    pipelineInfo.pMultisampleState = &ms;

    VkPipelineDepthStencilStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &ds;

    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    // no blend, write out all of rgba
    VkPipelineColorBlendAttachmentState att;
    memset(&att, 0, sizeof(att));
    att.colorWriteMask = 0xF;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    pipelineInfo.pColorBlendState = &cb;

    VkDynamicState dynEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn;
    memset(&dyn, 0, sizeof(dyn));
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = sizeof(dynEnable) / sizeof(VkDynamicState);
    dyn.pDynamicStates = dynEnable;
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_window->defaultRenderPass();

    err = m_devFuncs->vkCreateGraphicsPipelines(dev, m_pipelineCache, 1, &pipelineInfo, nullptr, &m_pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);

    if (vertShaderModule)
        m_devFuncs->vkDestroyShaderModule(dev, vertShaderModule, nullptr);
    if (fragShaderModule)
        m_devFuncs->vkDestroyShaderModule(dev, fragShaderModule, nullptr);
}

void TriangleRenderer::initSwapChainResources()
{
    qDebug("initSwapChainResources");

    // Projection matrix
    m_proj = m_window->clipCorrectionMatrix(); // adjust for Vulkan-OpenGL clip space differences
    const QSize sz = m_window->swapChainImageSize();
    m_proj.perspective(45.0f, sz.width() / (float) sz.height(), 0.01f, 100.0f);
    m_proj.translate(0, 0, -4);
}

void TriangleRenderer::releaseSwapChainResources()
{
    qDebug("releaseSwapChainResources");
}

void TriangleRenderer::releaseResources()
{
    qDebug("releaseResources");

    VkDevice dev = m_window->device();

    if (m_pipeline) {
        m_devFuncs->vkDestroyPipeline(dev, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout) {
        m_devFuncs->vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_pipelineCache) {
        m_devFuncs->vkDestroyPipelineCache(dev, m_pipelineCache, nullptr);
        m_pipelineCache = VK_NULL_HANDLE;
    }

    if (m_descSetLayout) {
        m_devFuncs->vkDestroyDescriptorSetLayout(dev, m_descSetLayout, nullptr);
        m_descSetLayout = VK_NULL_HANDLE;
    }

    if (m_descPool) {
        m_devFuncs->vkDestroyDescriptorPool(dev, m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }

    if (m_buf) {
        m_devFuncs->vkDestroyBuffer(dev, m_buf, nullptr);
        m_buf = VK_NULL_HANDLE;
    }

    if (m_bufMem) {
        m_devFuncs->vkFreeMemory(dev, m_bufMem, nullptr);
        m_bufMem = VK_NULL_HANDLE;
    }
}

void TriangleRenderer::startNextFrame()
{
    VkDevice dev = m_window->device();
    VkCommandBuffer cb = m_window->currentCommandBuffer();
    const QSize sz = m_window->swapChainImageSize();

    VkClearColorValue clearColor = {{ 0, 0, 0, 1 }};
    VkClearDepthStencilValue clearDS = { 1, 0 };
    VkClearValue clearValues[3];
    memset(clearValues, 0, sizeof(clearValues));
    clearValues[0].color = clearValues[2].color = clearColor;
    clearValues[1].depthStencil = clearDS;

    VkRenderPassBeginInfo rpBeginInfo;
    memset(&rpBeginInfo, 0, sizeof(rpBeginInfo));
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = m_window->defaultRenderPass();
    rpBeginInfo.framebuffer = m_window->currentFramebuffer();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = m_window->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    rpBeginInfo.pClearValues = clearValues;
    VkCommandBuffer cmdBuf = m_window->currentCommandBuffer();
    m_devFuncs->vkCmdBeginRenderPass(cmdBuf, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    quint8 *p;
    VkResult err = m_devFuncs->vkMapMemory(dev, m_bufMem, m_uniformBufInfo[m_window->currentFrame()].offset,
            UNIFORM_DATA_SIZE, 0, reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    QMatrix4x4 m = m_proj;
    m.rotate(m_rotation, 0, 1, 0);
    memcpy(p, m.constData(), 16 * sizeof(float));
    m_devFuncs->vkUnmapMemory(dev, m_bufMem);

    // Not exactly a real animation system, just advance on every frame for now.
    m_rotation += 1.0f;

    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    m_devFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                               &m_descSet[m_window->currentFrame()], 0, nullptr);
    VkDeviceSize vbOffset = 0;
    m_devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &m_buf, &vbOffset);

    VkViewport viewport;
    viewport.x = viewport.y = 0;
    viewport.width = sz.width();
    viewport.height = sz.height();
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    m_devFuncs->vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = viewport.width;
    scissor.extent.height = viewport.height;
    m_devFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

    m_devFuncs->vkCmdDraw(cb, 3, 1, 0, 0);

    m_devFuncs->vkCmdEndRenderPass(cmdBuf);

    m_window->frameReady();
    m_window->requestUpdate(); // render continuously, throttled by the presentation rate
}


VulkanRenderer::VulkanRenderer(VulkanWindow *w)
    : TriangleRenderer(w)
{
}

QVulkanWindowRenderer * VulkanWindow::createRenderer()
{
    return new VulkanRenderer(this);
}

void VulkanRenderer::initResources()
{
    TriangleRenderer::initResources();

    QVulkanInstance *inst = m_window->vulkanInstance();
    m_devFuncs = inst->deviceFunctions(m_window->device());

    QString info;
    info += QString::asprintf("Number of physical devices: %d\n", int(m_window->availablePhysicalDevices().count()));

    QVulkanFunctions *f = inst->functions();
    VkPhysicalDeviceProperties props;
    f->vkGetPhysicalDeviceProperties(m_window->physicalDevice(), &props);
    info += QString::asprintf("Active physical device name: '%s' version %d.%d.%d\nAPI version %d.%d.%d\n",
                              props.deviceName,
                              VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion),
                              VK_VERSION_PATCH(props.driverVersion),
                              VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                              VK_VERSION_PATCH(props.apiVersion));

    info += QStringLiteral("Supported instance layers:\n");
    for (const QVulkanLayer &layer : inst->supportedLayers())
        info += QString::asprintf("    %s v%u\n", layer.name.constData(), layer.version);
    info += QStringLiteral("Enabled instance layers:\n");
    for (const QByteArray &layer : inst->layers())
        info += QString::asprintf("    %s\n", layer.constData());

    info += QStringLiteral("Supported instance extensions:\n");
    for (const QVulkanExtension &ext : inst->supportedExtensions())
        info += QString::asprintf("    %s v%u\n", ext.name.constData(), ext.version);
    info += QStringLiteral("Enabled instance extensions:\n");
    for (const QByteArray &ext : inst->extensions())
        info += QString::asprintf("    %s\n", ext.constData());

    info += QString::asprintf("Color format: %u\nDepth-stencil format: %u\n",
                              m_window->colorFormat(), m_window->depthStencilFormat());

    info += QStringLiteral("Supported sample counts:");
    const QList<int> sampleCounts = m_window->supportedSampleCounts();
    for (int count : sampleCounts)
        info += QLatin1Char(' ') + QString::number(count);
    info += QLatin1Char('\n');

    //emit static_cast<VulkanWindow *>(m_window)->vulkanInfoReceived(info);
    LOG(LINFO, ("Vulkan INFO:", info.toStdString()));
}

void VulkanRenderer::startNextFrame()
{
    TriangleRenderer::startNextFrame();
    //emit static_cast<VulkanWindow *>(m_window)->frameQueued(int(m_rotation) % 360);
}
#endif
}  // namespace qt
