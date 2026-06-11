#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_project.h"
#include "ui_imagetile.h"
#include "project.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Ukrywamy centralny widget, aby docki przejęły cały ekran
    //ui->centralwidget->hide();
    //ui->centralwidget->setMinimumWidth(200);
    // Opcjonalnie: możesz mu nadać styl, żeby wyglądał estetycznie jako pusta przestrzeń robocza
    //ui->centralwidget->setStyleSheet("background-color: #1e1e1e;");

    this->setDockOptions(QMainWindow::AnimatedDocks |
                         QMainWindow::AllowNestedDocks |
                         QMainWindow::AllowTabbedDocks);
    this->setCentralWidget(nullptr);

    // PRZYPINKA ACTION: Panel Action ląduje na stałe po PRAWEJ stronie
    this->addDockWidget(Qt::RightDockWidgetArea, ui->dockWidgetAction);
    this->setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    //this->setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::West);

    // Testowe dodanie projektów
    this->addNewProject("Testowy Projekt 1");
    this->addNewProject("Testowy Projekt 2");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::addNewProject(QString projectName) {
    QDockWidget *newDock = new QDockWidget(projectName, this);

    newDock->setFeatures(QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetClosable |
                         QDockWidget::DockWidgetFloatable);

    Project *projectContent = new Project(newDock);
    newDock->setWidget(projectContent);

    if (openProjectsList.isEmpty()) {
        // PIERWSZY PROJEKT: Wrzucamy go do strefy po LEWEJ stronie.
        // Qt automatycznie rozciągnie go na całą lewą przestrzeń,
        // a panel Action zostanie zepchnięty na prawą krawędź.
        this->addDockWidget(Qt::LeftDockWidgetArea, newDock);
    } else {
        // KOLEJNE PROJEKTY: Nakładamy na pierwszy projekt jako zakładki (karty)
        this->tabifyDockWidget(openProjectsList.first(), newDock);
    }

    openProjectsList.append(newDock);

    QList<QTabBar*> mainBars = this->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly);
    if (!mainBars.isEmpty()) {
        QTabBar *tabBar = mainBars.first();
        tabBar->setTabsClosable(true);
        tabBar->disconnect(this);
        connect(tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::closeProjectByIndex);
        connect(tabBar, &QTabBar::tabBarDoubleClicked, this, &MainWindow::toggleProjectFloatState);
    }
}

void MainWindow::closeProjectByIndex(int index) {
    // Sprawdzamy, czy indeks jest poprawny
    if (index < 0 || index >= openProjectsList.size()) return;

    // 1. Pobieramy wskaźnik na zamykany dock z naszej listy
    QDockWidget *dockToRemove = openProjectsList.at(index);

    // 2. Usuwamy go z listy, żeby program o nim zapomniał
    openProjectsList.removeAt(index);

    // 3. Całkowicie i bezpiecznie kasujemy dock oraz jego zawartość (Project) z pamięci RAM
    dockToRemove->deleteLater();
}

void MainWindow::toggleProjectFloatState(int index) {
    if (index < 0 || index >= openProjectsList.size()) return;

    // Pobieramy dock przypisany do klikniętej zakładki
    QDockWidget *dock = openProjectsList.at(index);

    // Jeśli okno pływa (jest odczepione), przyczepiamy je z powrotem.
    // Jeśli jest w głównym oknie, odczepiamy je do osobnego okna.
    if (dock->isFloating()) {
        dock->setFloating(false);

        // Zabezpieczenie: upewniamy się, że po powrocie trafi do reszty projektów jako karta
        if (openProjectsList.size() > 1) {
            this->tabifyDockWidget(openProjectsList.first(), dock);
        }
    } else {
        dock->setFloating(true);
    }
}
