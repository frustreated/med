#include <iostream>
#include <cstdio>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
#include <QApplication>
#include <QtUiTools>
#include <QtDebug>

#include "gui/med-qt.hpp"
#include "gui/TreeItem.hpp"
#include "gui/TreeModel.hpp"
#include "gui/StoreTreeModel.hpp"
#include "gui/ComboBoxDelegate.hpp"
#include "gui/CheckBoxDelegate.hpp"
#include "med/med.hpp"

using namespace std;

/**
 * This will just perform the unlock by force
 */
void tryUnlock(std::mutex &mutex) {
  mutex.try_lock();
  mutex.unlock();
}


class ProcessDialogEventListener : public QObject {
  Q_OBJECT
public:
  ProcessDialogEventListener(MainUi* mainUi) {
    this->mainUi = mainUi;
  }
protected:
  bool eventFilter(QObject* obj, QEvent* ev) {
    QTreeWidget* processTreeWidget = mainUi->getProcessSelector()->findChild<QTreeWidget*>("processTreeWidget");
    if(obj == processTreeWidget && ev->type() == QEvent::KeyRelease) {
      if(static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Return) { //Use Return instead of Enter
        mainUi->onProcessItemDblClicked(processTreeWidget->currentItem(), 0); //Just use the first column
      }
    }
  }
private:
  MainUi* mainUi;
};

class MainWindowEventListener : public QObject {
  Q_OBJECT
public:
  MainWindowEventListener(MainUi* mainUi) {
    this->mainUi = mainUi;
  }
protected:
  bool eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::KeyRelease) {

      if (static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        if (mainUi->getScanState() == UiState::Editing) {
          tryUnlock(mainUi->scanUpdateMutex);
          mainUi->setScanState(UiState::Idle);
        }
        if (mainUi->getStoreState() == UiState::Editing) {
          tryUnlock(mainUi->storeUpdateMutex);
          mainUi->setStoreState(UiState::Idle);
        }
      }

      if (static_cast<QKeyEvent*>(ev)->key() == Qt::Key_F2) {
        QWidget* mainWindow = mainUi->getMainWindow();
        QWidget* focused = mainWindow->focusWidget()->parentWidget()->parentWidget(); // When the TreeView item value is selected, the TreeView will be the grandparent of the editing
        if (focused == mainWindow->findChild<QTreeView*>("scanTreeView")) {
          QModelIndex index = mainWindow->findChild<QTreeView*>("scanTreeView")->currentIndex();
          if (index.column() == SCAN_COL_VALUE) {
            mainUi->scanUpdateMutex.lock();
            mainUi->setScanState(UiState::Editing);
          }
        }
        else if (focused == mainWindow->findChild<QTreeView*>("storeTreeView")) {
          QModelIndex index = mainWindow->findChild<QTreeView*>("storeTreeView")->currentIndex();
          if (index.column() == STORE_COL_VALUE) {
            mainUi->storeUpdateMutex.lock();
            mainUi->setStoreState(UiState::Editing);
          }
        }
      }
    }
  }

private:
  MainUi* mainUi;
};


MainUi::MainUi() {
  loadUiFiles();
  loadProcessUi();
  setupStatusBar();
  setupScanTreeView();
  setupStoreTreeView();
  setupSignals();
  setupUi();

  scanState = UiState::Idle;
  storeState = UiState::Idle;
}

void MainUi::refresh(MainUi* mainUi) {
  while(1) {
    mainUi->refreshScanTreeView();
    mainUi->refreshStoreTreeView();
    std::this_thread::sleep_for(chrono::milliseconds(800));
  }
}

void MainUi::refreshScanTreeView() {
  scanUpdateMutex.lock();
  scanModel->refreshValues();
  scanUpdateMutex.unlock();
}

void MainUi::refreshStoreTreeView() {
  storeUpdateMutex.lock();
  storeModel->refreshValues();
  storeUpdateMutex.unlock();
}

void MainUi::onProcessClicked() {
  med.listProcesses();

  processDialog->show();

  //Get the tree widget
  QTreeWidget* processTreeWidget = processSelector->findChild<QTreeWidget*>("processTreeWidget");

  processTreeWidget->clear(); //Remove all items

  //Add all the process into the tree widget
  for(int i=med.processes.size()-1;i>=0;i--) {
    QTreeWidgetItem* item = new QTreeWidgetItem(processTreeWidget);
    item->setText(0, med.processes[i].pid.c_str());
    item->setText(1, med.processes[i].cmdline.c_str());
  }
}

void MainUi::onProcessItemDblClicked(QTreeWidgetItem* item, int column) {
  int index = item->treeWidget()->indexOfTopLevelItem(item); //Get the current row index
  med.selectedProcess = med.processes[med.processes.size() -1 - index];

  //Make changes to the selectedProcess and hide the window
  QLineEdit* line = this->mainWindow->findChild<QLineEdit*>("selectedProcess");
  line->setText(QString::fromLatin1((med.selectedProcess.pid + " " + med.selectedProcess.cmdline).c_str())); //Do not use fromStdString(), it will append with some unknown characters

  processDialog->hide();
}

void MainUi::onScanClicked() {
  scanUpdateMutex.lock();

  //Get scanned type
  string scanType = mainWindow->findChild<QComboBox*>("scanType")->currentText().toStdString();

  string scanValue = mainWindow->findChild<QLineEdit*>("scanEntry")->text().toStdString();

  if(med.selectedProcess.pid == "") {
    cerr << "No process seelcted " <<endl;
    return;
  }
  try {
    med.scan(scanValue, scanType);
  } catch(MedException &ex) {
    cerr << "scan: "<< ex.what() <<endl;
  }

  scanModel->clearAll();
  if(med.scanAddresses.size() <= 800) {
    scanModel->addScan(scanType);
  }

  updateNumberOfAddresses(mainWindow);
  scanUpdateMutex.unlock();
}

void MainUi::onFilterClicked() {
  scanUpdateMutex.lock();

  //Get scanned type
  string scanType = mainWindow->findChild<QComboBox*>("scanType")->currentText().toStdString();

  string scanValue = mainWindow->findChild<QLineEdit*>("scanEntry")->text().toStdString();

  med.filter(scanValue, scanType);

  if(med.scanAddresses.size() <= 800) {
    scanModel->addScan(scanType);
  }

  updateNumberOfAddresses(mainWindow);
  scanUpdateMutex.unlock();
}

void MainUi::onScanClearClicked() {
  scanUpdateMutex.lock();
  scanModel->empty();
  mainWindow->findChild<QStatusBar*>("statusbar")->showMessage("Scan cleared");
  scanUpdateMutex.unlock();
}
void MainUi::onStoreClearClicked() {
  storeUpdateMutex.lock();
  storeModel->empty();
  storeUpdateMutex.unlock();
}

void MainUi::onScanAddClicked() {
  auto indexes = mainWindow->findChild<QTreeView*>("scanTreeView")
    ->selectionModel()
    ->selectedRows(SCAN_COL_ADDRESS);

  scanUpdateMutex.lock();
  for (int i=0;i<indexes.size();i++) {
    med.addToStoreByIndex(indexes[i].row());
  }

  storeModel->refresh();
  scanUpdateMutex.unlock();
}

void MainUi::onScanAddAllClicked() {
  scanUpdateMutex.lock();
  for(auto i=0;i<med.scanAddresses.size();i++)
    med.addToStoreByIndex(i);
  storeModel->refresh();
  scanUpdateMutex.unlock();
}

void MainUi::onStoreNewClicked() {
  storeUpdateMutex.lock();
  med.addNewAddress();
  storeModel->addRow();
  storeUpdateMutex.unlock();
}

void MainUi::onStoreDeleteClicked() {
  auto indexes = mainWindow->findChild<QTreeView*>("storeTreeView")
    ->selectionModel()
    ->selectedRows(STORE_COL_ADDRESS);

  //Sort and reverse
  sort(indexes.begin(), indexes.end(), [](QModelIndex a, QModelIndex b) {
      return a.row() > b.row();
    });

  storeUpdateMutex.lock();
  for (auto i=0;i<indexes.size();i++) {
    med.deleteAddressByIndex(indexes[i].row());
  }
  storeModel->refresh();
  storeUpdateMutex.unlock();
}

void MainUi::onStoreShiftAllClicked() {
  //Get the from and to
  long shiftFrom, shiftTo, difference;
  try {
    shiftFrom = hexToInt(mainWindow->findChild<QLineEdit*>("shiftFrom")->text().toStdString());
    shiftTo = hexToInt(mainWindow->findChild<QLineEdit*>("shiftTo")->text().toStdString());
    difference = shiftTo - shiftFrom;
  } catch(MedException &e) {
    cout << e.what() << endl;
  }

  //Get PID
  if(med.selectedProcess.pid == "") {
    cerr<< "No PID" <<endl;
    return;
  }
  storeUpdateMutex.lock();
  med.shiftStoreAddresses(difference);
  storeModel->refresh();
  storeUpdateMutex.unlock();
}

void MainUi::onStoreShiftClicked() {
  //Get the from and to
  long shiftFrom, shiftTo, difference;
  try {
    shiftFrom = hexToInt(mainWindow->findChild<QLineEdit*>("shiftFrom")->text().toStdString());
    shiftTo = hexToInt(mainWindow->findChild<QLineEdit*>("shiftTo")->text().toStdString());
    difference = shiftTo - shiftFrom;
  } catch(MedException &e) {
    cout << e.what() << endl;
  }

  //Get PID
  if(med.selectedProcess.pid == "") {
    cerr<< "No PID" <<endl;
    return;
  }

  auto indexes = mainWindow->findChild<QTreeView*>("storeTreeView")
    ->selectionModel()
    ->selectedRows(STORE_COL_ADDRESS);

  storeUpdateMutex.lock();
  for (auto i=0;i<indexes.size();i++) {
    med.shiftStoreAddressByIndex(indexes[i].row(), difference);
  }
  storeModel->refresh();
  storeUpdateMutex.unlock();
}

void MainUi::onSaveAsTriggered() {
  if(med.selectedProcess.pid == "") {
    cerr<< "No PID" <<endl;
    return;
  }
  QString filename = QFileDialog::getSaveFileName(mainWindow,
                                                  QString("Save JSON"),
                                                  "./",
                                                  QString("Save JSON (*.json)"));

  med.saveFile(filename.toStdString().c_str());
}

void MainUi::onOpenTriggered() {
  if(med.selectedProcess.pid == "") {
    cerr<< "No PID" <<endl;
    return;
  }
  QString filename = QFileDialog::getOpenFileName(mainWindow,
                                                  QString("Open JSON"),
                                                  "./",
                                                  QString("Open JSON (*.json)"));
  med.openFile(filename.toStdString().c_str());

  storeUpdateMutex.lock();
  storeModel->clearAll();
  storeModel->refresh();
  storeUpdateMutex.unlock();
}

void MainUi::onScanTreeViewClicked(const QModelIndex &index) {
  if(index.column() == SCAN_COL_TYPE) {
    mainWindow->findChild<QTreeView*>("scanTreeView")->edit(index); //Trigger edit by 1 click
  }
}

void MainUi::onScanTreeViewDoubleClicked(const QModelIndex &index) {
  if (index.column() == SCAN_COL_VALUE) {
    scanUpdateMutex.lock();
    scanState = UiState::Editing;
  }
}

void MainUi::onStoreTreeViewDoubleClicked(const QModelIndex &index) {
  if (index.column() == STORE_COL_VALUE) {
    storeUpdateMutex.lock();
    storeState = UiState::Editing;
  }
}

void MainUi::onStoreTreeViewClicked(const QModelIndex &index) {
  if (index.column() == STORE_COL_TYPE) {
    mainWindow->findChild<QTreeView*>("storeTreeView")->edit(index);
  }
  else if (index.column() == STORE_COL_LOCK) {
    mainWindow->findChild<QTreeView*>("storeTreeView")->edit(index);
  }
}

void MainUi::onScanTreeViewDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // qDebug() << topLeft << bottomRight << roles;
  if (topLeft.column() == SCAN_COL_VALUE) {
    tryUnlock(scanUpdateMutex);
    scanState = UiState::Idle;
  }
}

void MainUi::onStoreTreeViewDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // qDebug() << topLeft << bottomRight << roles;
  if (topLeft.column() == STORE_COL_VALUE) {
    tryUnlock(storeUpdateMutex);
    storeState = UiState::Idle;
  }
}

void MainUi::onStoreHeaderClicked(int logicalIndex) {
  if (logicalIndex == STORE_COL_DESCRIPTION) {
    storeModel->sortByDescription();
  }
  else if (logicalIndex == STORE_COL_ADDRESS) {
    storeModel->sortByAddress();
  }
}

void MainUi::loadUiFiles() {
  QUiLoader loader;
  QFile file("./main-qt.ui");
  file.open(QFile::ReadOnly);
  mainWindow = loader.load(&file);
  file.close();
}

void MainUi::loadProcessUi() {
  QUiLoader loader;
  //Cannot put the followings to another method
  processDialog = new QDialog(mainWindow); //If put this to another method, then I cannot set the mainWindow as the parent
  QFile processFile("./process.ui");
  processFile.open(QFile::ReadOnly);

  processSelector = loader.load(&processFile, processDialog);
  processFile.close();

  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(processSelector);
  processDialog->setLayout(layout);
  processDialog->setModal(true);
  processDialog->resize(400, 400);

  //Add signal
  QTreeWidget* processTreeWidget = processSelector->findChild<QTreeWidget*>("processTreeWidget");
  QObject::connect(processTreeWidget, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)), this, SLOT(onProcessItemDblClicked(QTreeWidgetItem*, int)));

  processTreeWidget->installEventFilter(new ProcessDialogEventListener(this));

  mainWindow->installEventFilter(new MainWindowEventListener(this));
}

void MainUi::setupStatusBar() {
  //Statusbar message
  QStatusBar* statusBar = mainWindow->findChild<QStatusBar*>("statusbar");
  statusBar->showMessage("Tips: Left panel is scanned address. Right panel is stored address.");
}

void MainUi::setupScanTreeView() {
  scanModel = new TreeModel(&med, mainWindow);
  mainWindow->findChild<QTreeView*>("scanTreeView")->setModel(scanModel);
  ComboBoxDelegate* delegate = new ComboBoxDelegate();
  mainWindow->findChild<QTreeView*>("scanTreeView")->setItemDelegateForColumn(SCAN_COL_TYPE, delegate);
  QObject::connect(mainWindow->findChild<QTreeView*>("scanTreeView"),
                   SIGNAL(clicked(QModelIndex)),
                   this,
                   SLOT(onScanTreeViewClicked(QModelIndex)));
  QObject::connect(mainWindow->findChild<QTreeView*>("scanTreeView"),
                   SIGNAL(doubleClicked(QModelIndex)),
                   this,
                   SLOT(onScanTreeViewDoubleClicked(QModelIndex)));

  QObject::connect(scanModel,
                   SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)),
                   this,
                   SLOT(onScanTreeViewDataChanged(QModelIndex, QModelIndex, QVector<int>)));
  mainWindow->findChild<QTreeView*>("scanTreeView")->setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void MainUi::setupStoreTreeView() {
  storeModel = new StoreTreeModel(&med, mainWindow);
  mainWindow->findChild<QTreeView*>("storeTreeView")->setModel(storeModel);
  ComboBoxDelegate* storeDelegate = new ComboBoxDelegate();
  mainWindow->findChild<QTreeView*>("storeTreeView")->setItemDelegateForColumn(STORE_COL_TYPE, storeDelegate);
  CheckBoxDelegate* storeLockDelegate = new CheckBoxDelegate();
  mainWindow->findChild<QTreeView*>("storeTreeView")->setItemDelegateForColumn(STORE_COL_LOCK, storeLockDelegate);
  QObject::connect(mainWindow->findChild<QTreeView*>("storeTreeView"),
                   SIGNAL(clicked(QModelIndex)),
                   this,
                   SLOT(onStoreTreeViewClicked(QModelIndex)));
  QObject::connect(mainWindow->findChild<QTreeView*>("storeTreeView"),
                   SIGNAL(doubleClicked(QModelIndex)),
                   this,
                   SLOT(onStoreTreeViewDoubleClicked(QModelIndex)));

  QObject::connect(storeModel,
                   SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)),
                   this,
                   SLOT(onStoreTreeViewDataChanged(QModelIndex, QModelIndex, QVector<int>)));

  auto* header = mainWindow->findChild<QTreeView*>("storeTreeView")->header();
  header->setSectionsClickable(true);
  QObject::connect(header,
                   SIGNAL(sectionClicked(int)),
                   this,
                   SLOT(onStoreHeaderClicked(int)));

  mainWindow->findChild<QTreeView*>("storeTreeView")->setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void MainUi::setupSignals() {
  //Add signal to the process
  QObject::connect(mainWindow->findChild<QWidget*>("process"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onProcessClicked()));

  //Add signal to scan
  QObject::connect(mainWindow->findChild<QWidget*>("scanButton"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanClicked()));

  QObject::connect(mainWindow->findChild<QWidget*>("filterButton"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onFilterClicked()));

  QObject::connect(mainWindow->findChild<QWidget*>("scanClear"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanClearClicked())
                   );

  QObject::connect(mainWindow->findChild<QPushButton*>("scanAddAll"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanAddAllClicked())
                   );

  QObject::connect(mainWindow->findChild<QPushButton*>("scanAdd"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanAddClicked())
                   );

  QObject::connect(mainWindow->findChild<QPushButton*>("storeNew"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreNewClicked()));
  QObject::connect(mainWindow->findChild<QPushButton*>("storeDelete"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreDeleteClicked()));
  QObject::connect(mainWindow->findChild<QPushButton*>("storeClear"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreClearClicked()));

  QObject::connect(mainWindow->findChild<QPushButton*>("storeShiftAll"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreShiftAllClicked()));

  QObject::connect(mainWindow->findChild<QPushButton*>("storeShift"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreShiftClicked()));


  QObject::connect(mainWindow->findChild<QAction*>("actionSaveAs"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onSaveAsTriggered()));
  QObject::connect(mainWindow->findChild<QAction*>("actionOpen"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onOpenTriggered()));

}

void MainUi::setupUi() {

  //Set default scan type
  mainWindow->findChild<QComboBox*>("scanType")->setCurrentIndex(1);

  //TODO: center
  mainWindow->show();

  qRegisterMetaType<QVector<int>>(); //For multithreading.

  //Multi-threading
  refreshThread = new std::thread(MainUi::refresh, this);
}

void MainUi::updateNumberOfAddresses(QWidget* mainWindow) {
  char message[128];
  sprintf(message, "%ld addresses found", med.scanAddresses.size());
  mainWindow->findChild<QStatusBar*>("statusbar")->showMessage(message);
}

////////////////////////
// Accessors
////////////////////////

QWidget* MainUi::getProcessSelector() {
  return processSelector;
}

QWidget* MainUi::getMainWindow() {
  return mainWindow;
}

UiState MainUi::getScanState() {
  return scanState;
}
UiState MainUi::getStoreState() {
  return storeState;
}

void MainUi::setScanState(UiState state) {
  scanState = state;
}
void MainUi::setStoreState(UiState state) {
  storeState = state;
}

//////////////////////
// Main function
/////////////////////

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  MainUi* mainUi = new MainUi();
  return app.exec();
}

#include "med-qt.moc"