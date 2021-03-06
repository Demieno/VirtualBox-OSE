/* $Id: UIVirtualBoxManager.cpp $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxManager class implementation.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QMenuBar>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QStatusBar>
//# include <QToolButton>

/* GUI includes: */
#include "QIFileDialog.h"
#include "UIActionPoolManager.h"
#include "UICloudProfileManager.h"
#include "UIDesktopServices.h"
#include "UIExtraDataManager.h"
#include "UIHostNetworkManager.h"
#include "UIMedium.h"
#include "UIMediumManager.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UIVirtualBoxManager.h"
#include "UIVirtualBoxManagerWidget.h"
#include "UISettingsDialogSpecific.h"
#include "UIVMLogViewerDialog.h"
#include "UIVirtualMachineItem.h"
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
# include "UIUpdateManager.h"
#endif
#include "UIVirtualBoxEventHandler.h"
#include "UIWizardCloneVM.h"
#include "UIWizardExportApp.h"
#include "UIWizardImportApp.h"
#ifdef VBOX_WS_MAC
# include "UIImageTools.h"
# include "UIWindowMenuManager.h"
# include "VBoxUtils.h"
#endif
#ifdef VBOX_WS_X11
# include "UIDesktopWidgetWatchdog.h"
#endif
#ifndef VBOX_WS_MAC
# include "UIMenuBar.h"
#endif

/* COM includes: */
#include "CSystemProperties.h"

/* Other VBox stuff: */
#include <iprt/buildconfig.h>
#include <VBox/version.h>
#ifdef VBOX_WS_X11
# include <iprt/env.h>
#endif /* VBOX_WS_X11 */


/* static */
UIVirtualBoxManager *UIVirtualBoxManager::s_pInstance = 0;

/* static */
void UIVirtualBoxManager::create()
{
    /* Make sure VirtualBox Manager isn't created: */
    AssertReturnVoid(s_pInstance == 0);

    /* Create VirtualBox Manager: */
    new UIVirtualBoxManager;
    /* Prepare VirtualBox Manager: */
    s_pInstance->prepare();
    /* Show VirtualBox Manager: */
    s_pInstance->show();
    /* Register in the modal window manager: */
    windowManager().setMainWindowShown(s_pInstance);
}

/* static */
void UIVirtualBoxManager::destroy()
{
    /* Make sure VirtualBox Manager is created: */
    AssertPtrReturnVoid(s_pInstance);

    /* Unregister in the modal window manager: */
    windowManager().setMainWindowShown(0);
    /* Cleanup VirtualBox Manager: */
    s_pInstance->cleanup();
    /* Destroy machine UI: */
    delete s_pInstance;
}

UIVirtualBoxManager::UIVirtualBoxManager()
    : m_fPolished(false)
    , m_fFirstMediumEnumerationHandled(false)
    , m_pActionPool(0)
    , m_pManagerVirtualMedia(0)
    , m_pManagerHostNetwork(0)
    , m_pManagerCloudProfile(0)
{
    s_pInstance = this;
}

UIVirtualBoxManager::~UIVirtualBoxManager()
{
    s_pInstance = 0;
}

bool UIVirtualBoxManager::shouldBeMaximized() const
{
    return gEDataManager->selectorWindowShouldBeMaximized();
}

#ifdef VBOX_WS_MAC
bool UIVirtualBoxManager::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Ignore for non-active window except for FileOpen event which should be always processed: */
    if (!isActiveWindow() && pEvent->type() != QEvent::FileOpen)
        return QIWithRetranslateUI<QIMainWindow>::eventFilter(pObject, pEvent);

    /* Ignore for other objects: */
    if (qobject_cast<QWidget*>(pObject) &&
        qobject_cast<QWidget*>(pObject)->window() != this)
        return QIWithRetranslateUI<QIMainWindow>::eventFilter(pObject, pEvent);

    /* Which event do we have? */
    switch (pEvent->type())
    {
        case QEvent::FileOpen:
        {
            sltHandleOpenUrlCall(QList<QUrl>() << static_cast<QFileOpenEvent*>(pEvent)->url());
            pEvent->accept();
            return true;
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QIWithRetranslateUI<QIMainWindow>::eventFilter(pObject, pEvent);
}
#endif /* VBOX_WS_MAC */

void UIVirtualBoxManager::retranslateUi()
{
    /* Set window title: */
    QString strTitle(VBOX_PRODUCT);
    strTitle += " " + tr("Manager", "Note: main window title which is prepended by the product name.");
#ifdef VBOX_BLEEDING_EDGE
    strTitle += QString(" EXPERIMENTAL build ")
             +  QString(RTBldCfgVersion())
             +  QString(" r")
             +  QString(RTBldCfgRevisionStr())
             +  QString(" - " VBOX_BLEEDING_EDGE);
#endif /* VBOX_BLEEDING_EDGE */
    setWindowTitle(strTitle);
}

bool UIVirtualBoxManager::event(QEvent *pEvent)
{
    /* Which event do we have? */
    switch (pEvent->type())
    {
        /* Handle every ScreenChangeInternal event to notify listeners: */
        case QEvent::ScreenChangeInternal:
        {
            emit sigWindowRemapped();
            break;
        }
        default:
            break;
    }
    /* Call to base-class: */
    return QIWithRetranslateUI<QIMainWindow>::event(pEvent);
}

void UIVirtualBoxManager::moveEvent(QMoveEvent *pEvent)
{
    /* Call to base-class: */
    QIWithRetranslateUI<QIMainWindow>::moveEvent(pEvent);

#ifdef VBOX_WS_X11
    /* Prevent further handling if fake screen detected: */
    if (gpDesktop->isFakeScreenDetected())
        return;
#endif

    /* Prevent handling for yet/already invisible window or if window is in minimized state: */
    if (isVisible() && (windowState() & Qt::WindowMinimized) == 0)
    {
#if defined(VBOX_WS_MAC) || defined(VBOX_WS_WIN)
        m_geometry.moveTo(frameGeometry().x(), frameGeometry().y());
#else
        m_geometry.moveTo(geometry().x(), geometry().y());
#endif
    }
}

void UIVirtualBoxManager::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QIWithRetranslateUI<QIMainWindow>::resizeEvent(pEvent);

#ifdef VBOX_WS_X11
    /* Prevent handling if fake screen detected: */
    if (gpDesktop->isFakeScreenDetected())
        return;
#endif

    /* Prevent handling for yet/already invisible window or if window is in minimized state: */
    if (isVisible() && (windowState() & Qt::WindowMinimized) == 0)
    {
        QResizeEvent *pResizeEvent = static_cast<QResizeEvent*>(pEvent);
        m_geometry.setSize(pResizeEvent->size());
    }
}

void UIVirtualBoxManager::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QIWithRetranslateUI<QIMainWindow>::showEvent(pEvent);

    /* Is polishing required? */
    if (!m_fPolished)
    {
        /* Pass the show-event to polish-event: */
        polishEvent(pEvent);
        /* Mark as polished: */
        m_fPolished = true;
    }
}

void UIVirtualBoxManager::polishEvent(QShowEvent *)
{
    /* Make sure user warned about inaccessible media: */
    QMetaObject::invokeMethod(this, "sltHandleMediumEnumerationFinish", Qt::QueuedConnection);
}

void UIVirtualBoxManager::closeEvent(QCloseEvent *pEvent)
{
    /* Call to base-class: */
    QIWithRetranslateUI<QIMainWindow>::closeEvent(pEvent);

    /* Quit application: */
    QApplication::quit();
}

#ifdef VBOX_WS_X11
void UIVirtualBoxManager::sltHandleHostScreenAvailableAreaChange()
{
    /* Prevent handling if fake screen detected: */
    if (gpDesktop->isFakeScreenDetected())
        return;

    /* Restore the geometry cached by the window: */
    resize(m_geometry.size());
    move(m_geometry.topLeft());
}
#endif /* VBOX_WS_X11 */

void UIVirtualBoxManager::sltHandleMediumEnumerationFinish()
{
#if 0 // ohh, come on!
    /* To avoid annoying the user, we check for inaccessible media just once, after
     * the first media emumeration [started from main() at startup] is complete. */
    if (m_fFirstMediumEnumerationHandled)
        return;
    m_fFirstMediumEnumerationHandled = true;

    /* Make sure MM window/tool is not opened,
     * otherwise user sees everything himself: */
    if (   m_pManagerVirtualMedia
        || m_pWidget->isGlobalToolOpened(UIToolType_Media))
        return;

    /* Look for at least one inaccessible medium: */
    bool fIsThereAnyInaccessibleMedium = false;
    foreach (const QUuid &uMediumID, vboxGlobal().mediumIDs())
    {
        if (vboxGlobal().medium(uMediumID).state() == KMediumState_Inaccessible)
        {
            fIsThereAnyInaccessibleMedium = true;
            break;
        }
    }
    printf("Aaaaaaaaaaaaaaaaaaaaaaaaaaxxxxaaaaaaaa\n");
    /* Warn the user about inaccessible medium, propose to open MM window/tool: */
    if (fIsThereAnyInaccessibleMedium && msgCenter().warnAboutInaccessibleMedia())
    {
        /* Open the MM window: */
        sltOpenVirtualMediumManagerWindow();
    }
#endif
}

void UIVirtualBoxManager::sltHandleOpenUrlCall(QList<QUrl> list /* = QList<QUrl>() */)
{
    /* If passed list is empty, we take the one from VBoxGlobal: */
    if (list.isEmpty())
        list = vboxGlobal().takeArgumentUrls();

    /* Check if we are can handle the dropped urls: */
    for (int i = 0; i < list.size(); ++i)
    {
#ifdef VBOX_WS_MAC
        const QString strFile = ::darwinResolveAlias(list.at(i).toLocalFile());
#else
        const QString strFile = list.at(i).toLocalFile();
#endif
        /* If there is such file exists: */
        if (!strFile.isEmpty() && QFile::exists(strFile))
        {
            /* And has allowed VBox config file extension: */
            if (VBoxGlobal::hasAllowedExtension(strFile, VBoxFileExts))
            {
                /* Handle VBox config file: */
                CVirtualBox comVBox = vboxGlobal().virtualBox();
                CMachine comMachine = comVBox.FindMachine(strFile);
                if (comVBox.isOk() && comMachine.isNotNull())
                    vboxGlobal().launchMachine(comMachine);
                else
                    sltOpenAddMachineDialog(strFile);
            }
            /* And has allowed VBox OVF file extension: */
            else if (VBoxGlobal::hasAllowedExtension(strFile, OVFFileExts))
            {
                /* Allow only one file at the time: */
                sltOpenImportApplianceWizard(strFile);
                break;
            }
            /* And has allowed VBox extension pack file extension: */
            else if (VBoxGlobal::hasAllowedExtension(strFile, VBoxExtPackFileExts))
            {
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
                /* Prevent update manager from proposing us to update EP: */
                gUpdateManager->setEPInstallationRequested(true);
#endif
                /* Propose the user to install EP described by the arguments @a list. */
                vboxGlobal().doExtPackInstallation(strFile, QString(), this, NULL);
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
                /* Allow update manager to propose us to update EP: */
                gUpdateManager->setEPInstallationRequested(false);
#endif
            }
        }
    }
}

void UIVirtualBoxManager::sltHandleChooserPaneIndexChange()
{
    updateActionsVisibility();
    updateActionsAppearance();
}

void UIVirtualBoxManager::sltHandleGroupSavingProgressChange()
{
    updateActionsAppearance();
}

void UIVirtualBoxManager::sltHandleToolTypeChange()
{
    updateActionsVisibility();
    updateActionsAppearance();

    /* Make sure separate dialogs are closed when corresponding tools are opened: */
    switch (m_pWidget->toolsType())
    {
        case UIToolType_Media:   sltCloseVirtualMediumManagerWindow(); break;
        case UIToolType_Network: sltCloseHostNetworkManagerWindow(); break;
        case UIToolType_Cloud:   sltCloseCloudProfileManagerWindow(); break;
        case UIToolType_Logs:    sltCloseLogViewerWindow(); break;
        default: break;
    }
}

void UIVirtualBoxManager::sltCurrentSnapshotItemChange()
{
    updateActionsAppearance();
}

void UIVirtualBoxManager::sltHandleStateChange(const QUuid &)
{
    updateActionsAppearance();
}

void UIVirtualBoxManager::sltOpenVirtualMediumManagerWindow()
{
    /* First check if instance of widget opened the embedded way: */
    if (m_pWidget->isGlobalToolOpened(UIToolType_Media))
    {
        m_pWidget->setToolsType(UIToolType_Welcome);
        m_pWidget->closeGlobalTool(UIToolType_Media);
    }

    /* Create instance if not yet created: */
    if (!m_pManagerVirtualMedia)
    {
        UIMediumManagerFactory(m_pActionPool).prepare(m_pManagerVirtualMedia, this);
        connect(m_pManagerVirtualMedia, &QIManagerDialog::sigClose,
                this, &UIVirtualBoxManager::sltCloseVirtualMediumManagerWindow);
    }

    /* Show instance: */
    m_pManagerVirtualMedia->show();
    m_pManagerVirtualMedia->setWindowState(m_pManagerVirtualMedia->windowState() & ~Qt::WindowMinimized);
    m_pManagerVirtualMedia->activateWindow();
}

void UIVirtualBoxManager::sltCloseVirtualMediumManagerWindow()
{
    /* Destroy instance if still exists: */
    if (m_pManagerVirtualMedia)
        UIMediumManagerFactory().cleanup(m_pManagerVirtualMedia);
}

void UIVirtualBoxManager::sltOpenHostNetworkManagerWindow()
{
    /* First check if instance of widget opened the embedded way: */
    if (m_pWidget->isGlobalToolOpened(UIToolType_Network))
    {
        m_pWidget->setToolsType(UIToolType_Welcome);
        m_pWidget->closeGlobalTool(UIToolType_Network);
    }

    /* Create instance if not yet created: */
    if (!m_pManagerHostNetwork)
    {
        UIHostNetworkManagerFactory(m_pActionPool).prepare(m_pManagerHostNetwork, this);
        connect(m_pManagerHostNetwork, &QIManagerDialog::sigClose,
                this, &UIVirtualBoxManager::sltCloseHostNetworkManagerWindow);
    }

    /* Show instance: */
    m_pManagerHostNetwork->show();
    m_pManagerHostNetwork->setWindowState(m_pManagerHostNetwork->windowState() & ~Qt::WindowMinimized);
    m_pManagerHostNetwork->activateWindow();
}

void UIVirtualBoxManager::sltCloseHostNetworkManagerWindow()
{
    /* Destroy instance if still exists: */
    if (m_pManagerHostNetwork)
        UIHostNetworkManagerFactory().cleanup(m_pManagerHostNetwork);
}

void UIVirtualBoxManager::sltOpenCloudProfileManagerWindow()
{
    /* First check if instance of widget opened the embedded way: */
    if (m_pWidget->isGlobalToolOpened(UIToolType_Cloud))
    {
        m_pWidget->setToolsType(UIToolType_Welcome);
        m_pWidget->closeGlobalTool(UIToolType_Cloud);
    }

    /* Create instance if not yet created: */
    if (!m_pManagerCloudProfile)
    {
        UICloudProfileManagerFactory(m_pActionPool).prepare(m_pManagerCloudProfile, this);
        connect(m_pManagerCloudProfile, &QIManagerDialog::sigClose,
                this, &UIVirtualBoxManager::sltCloseCloudProfileManagerWindow);
        connect(m_pManagerCloudProfile, &QIManagerDialog::sigChange,
                this, &UIVirtualBoxManager::sigCloudProfileManagerChange);
    }

    /* Show instance: */
    m_pManagerCloudProfile->show();
    m_pManagerCloudProfile->setWindowState(m_pManagerCloudProfile->windowState() & ~Qt::WindowMinimized);
    m_pManagerCloudProfile->activateWindow();
}

void UIVirtualBoxManager::sltCloseCloudProfileManagerWindow()
{
    /* Destroy instance if still exists: */
    if (m_pManagerCloudProfile)
        UIHostNetworkManagerFactory().cleanup(m_pManagerCloudProfile);
}

void UIVirtualBoxManager::sltOpenImportApplianceWizard(const QString &strFileName /* = QString() */)
{
    /* Initialize variables: */
#ifdef VBOX_WS_MAC
    const QString strTmpFile = ::darwinResolveAlias(strFileName);
#else
    const QString strTmpFile = strFileName;
#endif

    /* Lock the action preventing cascade calls: */
    actionPool()->action(UIActionIndexST_M_File_S_ImportAppliance)->setProperty("opened", true);
    updateActionsAppearance();

    /* Use the "safe way" to open stack of Mac OS X Sheets: */
    QWidget *pWizardParent = windowManager().realParentWindow(this);
    UISafePointerWizardImportApp pWizard = new UIWizardImportApp(pWizardParent, strTmpFile);
    windowManager().registerNewParent(pWizard, pWizardParent);
    pWizard->prepare();
    if (strFileName.isEmpty() || pWizard->isValid())
        pWizard->exec();
    delete pWizard;

    /* Unlock the action allowing further calls: */
    if (actionPool())
    {
        actionPool()->action(UIActionIndexST_M_File_S_ImportAppliance)->setProperty("opened", QVariant());
        updateActionsAppearance();
    }
}

void UIVirtualBoxManager::sltOpenExportApplianceWizard()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();

    /* Populate the list of VM names: */
    QStringList names;
    for (int i = 0; i < items.size(); ++i)
        names << items.at(i)->name();

    /* Lock the action preventing cascade calls: */
    actionPool()->action(UIActionIndexST_M_File_S_ExportAppliance)->setProperty("opened", true);
    actionPool()->action(UIActionIndexST_M_Machine_S_ExportToOCI)->setProperty("opened", true);
    updateActionsAppearance();

    /* Check what was the action invoked us: */
    UIAction *pAction = qobject_cast<UIAction*>(sender());

    /* Use the "safe way" to open stack of Mac OS X Sheets: */
    QWidget *pWizardParent = windowManager().realParentWindow(this);
    UISafePointerWizard pWizard = new UIWizardExportApp(pWizardParent, names,
                                                        pAction &&
                                                        pAction == actionPool()->action(UIActionIndexST_M_Machine_S_ExportToOCI));
    windowManager().registerNewParent(pWizard, pWizardParent);
    pWizard->prepare();
    pWizard->exec();
    delete pWizard;

    /* Unlock the action allowing further calls: */
    if (actionPool())
    {
        actionPool()->action(UIActionIndexST_M_File_S_ExportAppliance)->setProperty("opened", QVariant());
        actionPool()->action(UIActionIndexST_M_Machine_S_ExportToOCI)->setProperty("opened", QVariant());
        updateActionsAppearance();
    }
}

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
void UIVirtualBoxManager::sltOpenExtraDataManagerWindow()
{
    gEDataManager->openWindow(this);
}
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

void UIVirtualBoxManager::sltOpenPreferencesDialog()
{
    /* Don't show the inaccessible warning
     * if the user tries to open global settings: */
    m_fFirstMediumEnumerationHandled = true;

    /* Lock the action preventing cascade calls: */
    actionPool()->action(UIActionIndex_M_Application_S_Preferences)->setProperty("opened", true);
    updateActionsAppearance();

    /* Create and execute global settings window: */
    QPointer<UISettingsDialogGlobal> pDlg = new UISettingsDialogGlobal(this);
    pDlg->execute();
    delete pDlg;

    /* Unlock the action allowing further calls: */
    if (actionPool())
    {
        actionPool()->action(UIActionIndex_M_Application_S_Preferences)->setProperty("opened", QVariant());
        updateActionsAppearance();
    }
}

void UIVirtualBoxManager::sltPerformExit()
{
    close();
}

void UIVirtualBoxManager::sltOpenAddMachineDialog(const QString &strFileName /* = QString() */)
{
    /* Initialize variables: */
#ifdef VBOX_WS_MAC
    QString strTmpFile = ::darwinResolveAlias(strFileName);
#else
    QString strTmpFile = strFileName;
#endif
    CVirtualBox comVBox = vboxGlobal().virtualBox();

    /* Lock the action preventing cascade calls: */
    actionPool()->action(UIActionIndexST_M_Welcome_S_Add)->setProperty("opened", true);
    updateActionsAppearance();

    /* No file specified: */
    if (strTmpFile.isEmpty())
    {
        QString strBaseFolder = comVBox.GetSystemProperties().GetDefaultMachineFolder();
        QString strTitle = tr("Select a virtual machine file");
        QStringList extensions;
        for (int i = 0; i < VBoxFileExts.size(); ++i)
            extensions << QString("*.%1").arg(VBoxFileExts[i]);
        QString strFilter = tr("Virtual machine files (%1)").arg(extensions.join(" "));
        /* Create open file dialog: */
        QStringList fileNames = QIFileDialog::getOpenFileNames(strBaseFolder, strFilter, this, strTitle, 0, true, true);
        if (!fileNames.isEmpty())
            strTmpFile = fileNames.at(0);
    }

    /* Unlock the action allowing further calls: */
    if (actionPool())
    {
        actionPool()->action(UIActionIndexST_M_Welcome_S_Add)->setProperty("opened", QVariant());
        updateActionsAppearance();
    }

    /* Nothing was chosen? */
    if (strTmpFile.isEmpty())
        return;

    /* Make sure this machine can be opened: */
    CMachine comMachineNew = comVBox.OpenMachine(strTmpFile);
    if (!comVBox.isOk())
    {
        msgCenter().cannotOpenMachine(comVBox, strTmpFile);
        return;
    }

    /* Make sure this machine was NOT registered already: */
    CMachine comMachineOld = comVBox.FindMachine(comMachineNew.GetId().toString());
    if (!comMachineOld.isNull())
    {
        msgCenter().cannotReregisterExistingMachine(strTmpFile, comMachineOld.GetName());
        return;
    }

    /* Register that machine: */
    comVBox.RegisterMachine(comMachineNew);
}

void UIVirtualBoxManager::sltOpenMachineSettingsDialog(QString strCategory /* = QString() */,
                                                       QString strControl /* = QString() */,
                                                       const QUuid &uID /* = QString() */)
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* Lock the action preventing cascade calls: */
    actionPool()->action(UIActionIndexST_M_Machine_S_Settings)->setProperty("opened", true);
    updateActionsAppearance();

    /* Process href from VM details / description: */
    if (!strCategory.isEmpty() && strCategory[0] != '#')
    {
        vboxGlobal().openURL(strCategory);
    }
    else
    {
        /* Check if control is coded into the URL by %%: */
        if (strControl.isEmpty())
        {
            QStringList parts = strCategory.split("%%");
            if (parts.size() == 2)
            {
                strCategory = parts.at(0);
                strControl = parts.at(1);
            }
        }

        /* Don't show the inaccessible warning
         * if the user tries to open VM settings: */
        m_fFirstMediumEnumerationHandled = true;

        /* Create and execute corresponding VM settings window: */
        QPointer<UISettingsDialogMachine> pDlg = new UISettingsDialogMachine(this,
                                                                             uID.isNull() ? pItem->id() : uID,
                                                                             strCategory, strControl);
        pDlg->execute();
        delete pDlg;
    }

    /* Unlock the action allowing further calls: */
    if (actionPool())
    {
        actionPool()->action(UIActionIndexST_M_Machine_S_Settings)->setProperty("opened", QVariant());
        updateActionsAppearance();
    }
}

void UIVirtualBoxManager::sltOpenCloneMachineWizard()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* Use the "safe way" to open stack of Mac OS X Sheets: */
    QWidget *pWizardParent = windowManager().realParentWindow(this);
    const QStringList &machineGroupNames = pItem->groups();
    const QString strGroup = !machineGroupNames.isEmpty() ? machineGroupNames.at(0) : QString();
    UISafePointerWizard pWizard = new UIWizardCloneVM(pWizardParent, pItem->machine(), strGroup);
    windowManager().registerNewParent(pWizard, pWizardParent);
    pWizard->prepare();
    pWizard->exec();
    delete pWizard;
}

void UIVirtualBoxManager::sltPerformMachineMove()
{
    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();
    AssertMsgReturnVoid(pItem, ("Current item should be selected!\n"));

    /* Open a session thru which we will modify the machine: */
    CSession comSession = vboxGlobal().openSession(pItem->id(), KLockType_Write);
    if (comSession.isNull())
        return;

    /* Get session machine: */
    CMachine comMachine = comSession.GetMachine();
    AssertMsgReturnVoid(comSession.isOk() && comMachine.isNotNull(), ("Unable to acquire machine!\n"));

    /* Open a file dialog for the user to select a destination folder. Start with the default machine folder: */
    CVirtualBox comVBox = vboxGlobal().virtualBox();
    QString strBaseFolder = comVBox.GetSystemProperties().GetDefaultMachineFolder();
    QString strTitle = tr("Select a destination folder to move the selected virtual machine");
    QString strDestinationFolder = QIFileDialog::getExistingDirectory(strBaseFolder, this, strTitle);
    if (!strDestinationFolder.isEmpty())
    {
        /* Prepare machine move progress: */
        CProgress comProgress = comMachine.MoveTo(strDestinationFolder, "basic");
        if (comMachine.isOk() && comProgress.isNotNull())
        {
            /* Show machine move progress: */
            msgCenter().showModalProgressDialog(comProgress, comMachine.GetName(), ":/progress_clone_90px.png");
            if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
                msgCenter().cannotMoveMachine(comProgress, comMachine.GetName());
        }
        else
            msgCenter().cannotMoveMachine(comMachine);
    }
    comSession.UnlockMachine();
}

void UIVirtualBoxManager::sltPerformStartOrShowMachine()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartOrShowVirtualMachines(items, VBoxGlobal::LaunchMode_Invalid);
}

void UIVirtualBoxManager::sltPerformStartMachineNormal()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartOrShowVirtualMachines(items, VBoxGlobal::LaunchMode_Default);
}

void UIVirtualBoxManager::sltPerformStartMachineHeadless()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartOrShowVirtualMachines(items, VBoxGlobal::LaunchMode_Headless);
}

void UIVirtualBoxManager::sltPerformStartMachineDetachable()
{
    /* Start selected VMs in corresponding mode: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));
    performStartOrShowVirtualMachines(items, VBoxGlobal::LaunchMode_Separate);
}

void UIVirtualBoxManager::sltPerformDiscardMachineState()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be discarded: */
    QStringList machineNames;
    QList<UIVirtualMachineItem*> itemsToDiscard;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexST_M_Group_S_Discard, QList<UIVirtualMachineItem*>() << pItem))
        {
            machineNames << pItem->name();
            itemsToDiscard << pItem;
        }
    }
    AssertMsg(!machineNames.isEmpty(), ("This action should not be allowed!"));

    /* Confirm discarding saved VM state: */
    if (!msgCenter().confirmDiscardSavedState(machineNames.join(", ")))
        return;

    /* For every confirmed item: */
    foreach (UIVirtualMachineItem *pItem, itemsToDiscard)
    {
        /* Open a session to modify VM: */
        CSession comSession = vboxGlobal().openSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session machine: */
        CMachine comMachine = comSession.GetMachine();
        comMachine.DiscardSavedState(true);
        if (!comMachine.isOk())
            msgCenter().cannotDiscardSavedState(comMachine);

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformPauseOrResumeMachine(bool fPause)
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For every selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Get item state: */
        const KMachineState enmState = pItem->machineState();

        /* Check if current item could be paused/resumed: */
        if (!isActionEnabled(UIActionIndexST_M_Group_T_Pause, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Check if current item already paused: */
        if (fPause &&
            (enmState == KMachineState_Paused ||
             enmState == KMachineState_TeleportingPausedVM))
            continue;

        /* Check if current item already resumed: */
        if (!fPause &&
            (enmState == KMachineState_Running ||
             enmState == KMachineState_Teleporting ||
             enmState == KMachineState_LiveSnapshotting))
            continue;

        /* Open a session to modify VM state: */
        CSession comSession = vboxGlobal().openExistingSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session console: */
        CConsole comConsole = comSession.GetConsole();
        /* Pause/resume VM: */
        if (fPause)
            comConsole.Pause();
        else
            comConsole.Resume();
        if (!comConsole.isOk())
        {
            if (fPause)
                msgCenter().cannotPauseMachine(comConsole);
            else
                msgCenter().cannotResumeMachine(comConsole);
        }

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformResetMachine()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be reseted: */
    QStringList machineNames;
    QList<UIVirtualMachineItem*> itemsToReset;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexST_M_Group_S_Reset, QList<UIVirtualMachineItem*>() << pItem))
        {
            machineNames << pItem->name();
            itemsToReset << pItem;
        }
    }
    AssertMsg(!machineNames.isEmpty(), ("This action should not be allowed!"));

    /* Confirm reseting VM: */
    if (!msgCenter().confirmResetMachine(machineNames.join(", ")))
        return;

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, itemsToReset)
    {
        /* Open a session to modify VM state: */
        CSession comSession = vboxGlobal().openExistingSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session console: */
        CConsole comConsole = comSession.GetConsole();
        /* Reset VM: */
        comConsole.Reset();

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformDetachMachineUI()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Check if current item could be detached: */
        if (!isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_Detach, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /// @todo Detach separate UI process..
        AssertFailed();
    }
}

void UIVirtualBoxManager::sltPerformSaveMachineState()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Check if current item could be saved: */
        if (!isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_SaveState, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Open a session to modify VM state: */
        CSession comSession = vboxGlobal().openExistingSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session console: */
        CConsole comConsole = comSession.GetConsole();
        /* Get session machine: */
        CMachine comMachine = comSession.GetMachine();
        /* Pause VM first if necessary: */
        if (pItem->machineState() != KMachineState_Paused)
            comConsole.Pause();
        if (comConsole.isOk())
        {
            /* Prepare machine state saving progress: */
            CProgress comProgress = comMachine.SaveState();
            if (comMachine.isOk())
            {
                /* Show machine state saving progress: */
                msgCenter().showModalProgressDialog(comProgress, comMachine.GetName(), ":/progress_state_save_90px.png");
                if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
                    msgCenter().cannotSaveMachineState(comProgress, comMachine.GetName());
            }
            else
                msgCenter().cannotSaveMachineState(comMachine);
        }
        else
            msgCenter().cannotPauseMachine(comConsole);

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformShutdownMachine()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be shutdowned: */
    QStringList machineNames;
    QList<UIVirtualMachineItem*> itemsToShutdown;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_Shutdown, QList<UIVirtualMachineItem*>() << pItem))
        {
            machineNames << pItem->name();
            itemsToShutdown << pItem;
        }
    }
    AssertMsg(!machineNames.isEmpty(), ("This action should not be allowed!"));

    /* Confirm ACPI shutdown current VM: */
    if (!msgCenter().confirmACPIShutdownMachine(machineNames.join(", ")))
        return;

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, itemsToShutdown)
    {
        /* Open a session to modify VM state: */
        CSession comSession = vboxGlobal().openExistingSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session console: */
        CConsole comConsole = comSession.GetConsole();
        /* ACPI Shutdown: */
        comConsole.PowerButton();
        if (!comConsole.isOk())
            msgCenter().cannotACPIShutdownMachine(comConsole);

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformPowerOffMachine()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* Prepare the list of the machines to be powered off: */
    QStringList machineNames;
    QList<UIVirtualMachineItem*> itemsToPowerOff;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_PowerOff, QList<UIVirtualMachineItem*>() << pItem))
        {
            machineNames << pItem->name();
            itemsToPowerOff << pItem;
        }
    }
    AssertMsg(!machineNames.isEmpty(), ("This action should not be allowed!"));

    /* Confirm Power Off current VM: */
    if (!msgCenter().confirmPowerOffMachine(machineNames.join(", ")))
        return;

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, itemsToPowerOff)
    {
        /* Open a session to modify VM state: */
        CSession comSession = vboxGlobal().openExistingSession(pItem->id());
        if (comSession.isNull())
            return;

        /* Get session console: */
        CConsole comConsole = comSession.GetConsole();
        /* Prepare machine power down: */
        CProgress comProgress = comConsole.PowerDown();
        if (comConsole.isOk())
        {
            /* Show machine power down progress: */
            CMachine machine = comSession.GetMachine();
            msgCenter().showModalProgressDialog(comProgress, machine.GetName(), ":/progress_poweroff_90px.png");
            if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
                msgCenter().cannotPowerDownMachine(comProgress, machine.GetName());
        }
        else
            msgCenter().cannotPowerDownMachine(comConsole);

        /* Unlock machine finally: */
        comSession.UnlockMachine();
    }
}

void UIVirtualBoxManager::sltPerformShowMachineTool(QAction *pAction)
{
    AssertPtrReturnVoid(pAction);
    AssertPtrReturnVoid(m_pWidget);
    m_pWidget->setToolsType(pAction->property("UIToolType").value<UIToolType>());
}

void UIVirtualBoxManager::sltOpenLogViewerWindow()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* First check if instance of widget opened the embedded way: */
    if (m_pWidget->isMachineToolOpened(UIToolType_Logs))
    {
        m_pWidget->setToolsType(UIToolType_Details);
        m_pWidget->closeMachineTool(UIToolType_Logs);
    }

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Check if log could be show for the current item: */
        if (!isActionEnabled(UIActionIndexST_M_Group_S_ShowLogDialog, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        QIManagerDialog *pLogViewerDialog = 0;
        /* Create and Show VM Log Viewer: */
        if (!m_logViewers[pItem->machine().GetHardwareUUID().toString()])
        {
            UIVMLogViewerDialogFactory dialogFactory(actionPool(), pItem->machine());
            dialogFactory.prepare(pLogViewerDialog, this);
            if (pLogViewerDialog)
            {
                m_logViewers[pItem->machine().GetHardwareUUID().toString()] = pLogViewerDialog;
                connect(pLogViewerDialog, &QIManagerDialog::sigClose,
                        this, &UIVirtualBoxManager::sltCloseLogViewerWindow);
            }
        }
        else
        {
            pLogViewerDialog = m_logViewers[pItem->machine().GetHardwareUUID().toString()];
        }
        if (pLogViewerDialog)
        {
            /* Show instance: */
            pLogViewerDialog->show();
            pLogViewerDialog->setWindowState(pLogViewerDialog->windowState() & ~Qt::WindowMinimized);
            pLogViewerDialog->activateWindow();
        }
    }
}

void UIVirtualBoxManager::sltCloseLogViewerWindow()
{
    /* If there is a proper sender: */
    if (qobject_cast<QIManagerDialog*>(sender()))
    {
        /* Search for the sender of the signal within the m_logViewers map: */
        QMap<QString, QIManagerDialog*>::iterator sendersIterator = m_logViewers.begin();
        while (sendersIterator != m_logViewers.end() && sendersIterator.value() != sender())
            ++sendersIterator;
        /* Do nothing if we cannot find it with the map: */
        if (sendersIterator == m_logViewers.end())
            return;

        /* Check whether we have found the proper dialog: */
        QIManagerDialog *pDialog = qobject_cast<QIManagerDialog*>(sendersIterator.value());
        if (!pDialog)
            return;

        /* First remove this log-viewer dialog from the map.
         * This should be done before closing the dialog which will incur
         * a second call to this function and result in double delete!!! */
        m_logViewers.erase(sendersIterator);
        UIVMLogViewerDialogFactory().cleanup(pDialog);
    }
    /* Otherwise: */
    else
    {
        /* Just wipe out everything: */
        foreach (const QString &strKey, m_logViewers.keys())
        {
            /* First remove each log-viewer dialog from the map.
             * This should be done before closing the dialog which will incur
             * a second call to this function and result in double delete!!! */
            QIManagerDialog *pDialog = m_logViewers.value(strKey);
            m_logViewers.remove(strKey);
            UIVMLogViewerDialogFactory().cleanup(pDialog);
        }
    }
}

void UIVirtualBoxManager::sltShowMachineInFileManager()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Check if that item could be shown in file-browser: */
        if (!isActionEnabled(UIActionIndexST_M_Group_S_ShowInFileManager, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Show VM in filebrowser: */
        UIDesktopServices::openInFileManager(pItem->machine().GetSettingsFilePath());
    }
}

void UIVirtualBoxManager::sltPerformCreateMachineShortcut()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    /* For each selected item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Check if shortcuts could be created for this item: */
        if (!isActionEnabled(UIActionIndexST_M_Group_S_CreateShortcut, QList<UIVirtualMachineItem*>() << pItem))
            continue;

        /* Create shortcut for this VM: */
        const CMachine &comMachine = pItem->machine();
        UIDesktopServices::createMachineShortcut(comMachine.GetSettingsFilePath(),
                                                 QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                                                 comMachine.GetName(), comMachine.GetId());
    }
}

void UIVirtualBoxManager::sltGroupCloseMenuAboutToShow()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    actionPool()->action(UIActionIndexST_M_Group_M_Close_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_Close_S_Shutdown, items));
}

void UIVirtualBoxManager::sltMachineCloseMenuAboutToShow()
{
    /* Get selected items: */
    QList<UIVirtualMachineItem*> items = currentItems();
    AssertMsgReturnVoid(!items.isEmpty(), ("At least one item should be selected!\n"));

    actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_Shutdown, items));
}

void UIVirtualBoxManager::prepare()
{
#ifdef VBOX_WS_X11
    /* Assign same name to both WM_CLASS name & class for now: */
    VBoxGlobal::setWMClass(this, "VirtualBox Manager", "VirtualBox Manager");
#endif

#ifdef VBOX_WS_MAC
    /* We have to make sure that we are getting the front most process: */
    ::darwinSetFrontMostProcess();
    /* Install global event-filter, since vmstarter.app can send us FileOpen events,
     * see UIVirtualBoxManager::eventFilter for handler implementation. */
    qApp->installEventFilter(this);
#endif

    /* Cache medium data early if necessary: */
    if (vboxGlobal().agressiveCaching())
        vboxGlobal().startMediumEnumeration();

    /* Prepare: */
    prepareIcon();
    prepareMenuBar();
    prepareStatusBar();
    prepareWidgets();
    prepareConnections();

    /* Update actions initially: */
    updateActionsVisibility();
    updateActionsAppearance();

    /* Load settings: */
    loadSettings();

    /* Translate UI: */
    retranslateUi();

#ifdef VBOX_WS_MAC
    /* Beta label? */
    if (vboxGlobal().isBeta())
    {
        QPixmap betaLabel = ::betaLabel(QSize(100, 16));
        ::darwinLabelWindow(this, &betaLabel, true);
    }
#endif /* VBOX_WS_MAC */

    /* If there are unhandled URLs we should handle them after manager is shown: */
    if (vboxGlobal().argumentUrlsPresent())
        QMetaObject::invokeMethod(this, "sltHandleOpenUrlCall", Qt::QueuedConnection);
}

void UIVirtualBoxManager::prepareIcon()
{
    /* Prepare application icon.
     * On Win host it's built-in to the executable.
     * On Mac OS X the icon referenced in info.plist is used.
     * On X11 we will provide as much icons as we can. */
#if !defined(VBOX_WS_WIN) && !defined(VBOX_WS_MAC)
    QIcon icon(":/VirtualBox.svg");
    icon.addFile(":/VirtualBox_48px.png");
    icon.addFile(":/VirtualBox_64px.png");
    setWindowIcon(icon);
#endif /* !VBOX_WS_WIN && !VBOX_WS_MAC */
}

void UIVirtualBoxManager::prepareMenuBar()
{
#ifndef VBOX_WS_MAC
    /* Create menu-bar: */
    setMenuBar(new UIMenuBar);
    if (menuBar())
    {
        /* Make sure menu-bar fills own solid background: */
        menuBar()->setAutoFillBackground(true);
        QPalette pal = menuBar()->palette();
        const QColor color = pal.color(QPalette::Active, QPalette::Mid).lighter(160);
        pal.setColor(QPalette::Active, QPalette::Button, color);
        menuBar()->setPalette(pal);
    }
#endif

    /* Create action-pool: */
    m_pActionPool = UIActionPool::create(UIActionPoolType_Manager);

    /* Build menu-bar: */
    foreach (QMenu *pMenu, actionPool()->menus())
    {
#ifdef VBOX_WS_MAC
        /* Before 'Help' menu we should: */
        if (pMenu == actionPool()->action(UIActionIndex_Menu_Help)->menu())
        {
            /* Insert 'Window' menu: */
            UIWindowMenuManager::create();
            menuBar()->addMenu(gpWindowMenuManager->createMenu(this));
            gpWindowMenuManager->addWindow(this);
        }
#endif
        menuBar()->addMenu(pMenu);
    }

    /* Setup menu-bar policy: */
    menuBar()->setContextMenuPolicy(Qt::CustomContextMenu);
}

void UIVirtualBoxManager::prepareStatusBar()
{
    /* We are not using status-bar anymore: */
    statusBar()->setHidden(true);
}

void UIVirtualBoxManager::prepareWidgets()
{
    /* Create central-widget: */
    m_pWidget = new UIVirtualBoxManagerWidget(this);
    if (m_pWidget)
    {
        /* Configure central-widget: */
        connect(m_pWidget, &UIVirtualBoxManagerWidget::sigCloudProfileManagerChange,
                this, &UIVirtualBoxManager::sigCloudProfileManagerChange);
        connect(m_pWidget, &UIVirtualBoxManagerWidget::sigCurrentSnapshotItemChange,
                this, &UIVirtualBoxManager::sltCurrentSnapshotItemChange);
        setCentralWidget(m_pWidget);
    }
}

void UIVirtualBoxManager::prepareConnections()
{
#ifdef VBOX_WS_X11
    /* Desktop event handlers: */
    connect(gpDesktop, &UIDesktopWidgetWatchdog::sigHostScreenWorkAreaResized,
            this, &UIVirtualBoxManager::sltHandleHostScreenAvailableAreaChange);
#endif

    /* Medium enumeration connections: */
    connect(&vboxGlobal(), &VBoxGlobal::sigMediumEnumerationFinished,
            this, &UIVirtualBoxManager::sltHandleMediumEnumerationFinish);

    /* Widget connections: */
    connect(m_pWidget, &UIVirtualBoxManagerWidget::sigChooserPaneIndexChange,
            this, &UIVirtualBoxManager::sltHandleChooserPaneIndexChange);
    connect(m_pWidget, &UIVirtualBoxManagerWidget::sigGroupSavingStateChanged,
            this, &UIVirtualBoxManager::sltHandleGroupSavingProgressChange);
    connect(m_pWidget, &UIVirtualBoxManagerWidget::sigMachineSettingsLinkClicked,
            this, &UIVirtualBoxManager::sltOpenMachineSettingsDialog);
    connect(m_pWidget, &UIVirtualBoxManagerWidget::sigToolTypeChange,
            this, &UIVirtualBoxManager::sltHandleToolTypeChange);
    connect(menuBar(), &QMenuBar::customContextMenuRequested,
            m_pWidget, &UIVirtualBoxManagerWidget::sltHandleContextMenuRequest);

    /* Global VBox event handlers: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIVirtualBoxManager::sltHandleStateChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSessionStateChange,
            this, &UIVirtualBoxManager::sltHandleStateChange);

    /* 'File' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_File_S_ShowVirtualMediumManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenVirtualMediumManagerWindow);
    connect(actionPool()->action(UIActionIndexST_M_File_S_ShowHostNetworkManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenHostNetworkManagerWindow);
    connect(actionPool()->action(UIActionIndexST_M_File_S_ShowCloudProfileManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenCloudProfileManagerWindow);
    connect(actionPool()->action(UIActionIndexST_M_File_S_ImportAppliance), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenImportApplianceWizardDefault);
    connect(actionPool()->action(UIActionIndexST_M_File_S_ExportAppliance), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenExportApplianceWizard);
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    connect(actionPool()->action(UIActionIndexST_M_File_S_ShowExtraDataManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenExtraDataManagerWindow);
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */
    connect(actionPool()->action(UIActionIndex_M_Application_S_Preferences), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenPreferencesDialog);
    connect(actionPool()->action(UIActionIndexST_M_File_S_Close), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformExit);

    /* 'Welcome' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_Welcome_S_Add), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenAddMachineDialogDefault);

    /* 'Group' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_Group_S_Add), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenAddMachineDialogDefault);
    connect(actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartOrShowMachine);
    connect(actionPool()->action(UIActionIndexST_M_Group_T_Pause), &UIAction::toggled,
            this, &UIVirtualBoxManager::sltPerformPauseOrResumeMachine);
    connect(actionPool()->action(UIActionIndexST_M_Group_S_Reset), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformResetMachine);
    connect(actionPool()->action(UIActionIndexST_M_Group_S_Discard), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDiscardMachineState);
    connect(actionPool()->action(UIActionIndexST_M_Group_S_ShowLogDialog), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenLogViewerWindow);
    connect(actionPool()->action(UIActionIndexST_M_Group_S_ShowInFileManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltShowMachineInFileManager);
    connect(actionPool()->action(UIActionIndexST_M_Group_S_CreateShortcut), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCreateMachineShortcut);

    /* 'Machine' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Add), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenAddMachineDialogDefault);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Settings), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenMachineSettingsDialogDefault);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Clone), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenCloneMachineWizard);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Move), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformMachineMove);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_ExportToOCI), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenExportApplianceWizard);
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartOrShowMachine);
    connect(actionPool()->action(UIActionIndexST_M_Machine_T_Pause), &UIAction::toggled,
            this, &UIVirtualBoxManager::sltPerformPauseOrResumeMachine);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Reset), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformResetMachine);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Discard), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDiscardMachineState);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_ShowLogDialog), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltOpenLogViewerWindow);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_ShowInFileManager), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltShowMachineInFileManager);
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_CreateShortcut), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformCreateMachineShortcut);

    /* 'Group/Start or Show' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow_S_StartNormal), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineNormal);
    connect(actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow_S_StartHeadless), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineHeadless);
    connect(actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow_S_StartDetachable), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineDetachable);

    /* 'Machine/Start or Show' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow_S_StartNormal), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineNormal);
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow_S_StartHeadless), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineHeadless);
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow_S_StartDetachable), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformStartMachineDetachable);

    /* 'Group/Close' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_Group_M_Close)->menu(), &UIMenu::aboutToShow,
            this, &UIVirtualBoxManager::sltGroupCloseMenuAboutToShow);
    connect(actionPool()->action(UIActionIndexST_M_Group_M_Close_S_Detach), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDetachMachineUI);
    connect(actionPool()->action(UIActionIndexST_M_Group_M_Close_S_SaveState), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformSaveMachineState);
    connect(actionPool()->action(UIActionIndexST_M_Group_M_Close_S_Shutdown), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShutdownMachine);
    connect(actionPool()->action(UIActionIndexST_M_Group_M_Close_S_PowerOff), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformPowerOffMachine);

    /* 'Machine/Close' menu connections: */
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_Close)->menu(), &UIMenu::aboutToShow,
            this, &UIVirtualBoxManager::sltMachineCloseMenuAboutToShow);
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_Detach), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformDetachMachineUI);
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_SaveState), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformSaveMachineState);
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_Shutdown), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformShutdownMachine);
    connect(actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_PowerOff), &UIAction::triggered,
            this, &UIVirtualBoxManager::sltPerformPowerOffMachine);

    /* 'Group/Tools' menu connections: */
    connect(actionPool()->actionGroup(UIActionIndexST_M_Group_M_Tools), &QActionGroup::triggered,
            this, &UIVirtualBoxManager::sltPerformShowMachineTool);

    /* 'Machine/Tools' menu connections: */
    connect(actionPool()->actionGroup(UIActionIndexST_M_Machine_M_Tools), &QActionGroup::triggered,
            this, &UIVirtualBoxManager::sltPerformShowMachineTool);
}

void UIVirtualBoxManager::loadSettings()
{
    /* Restore window geometry: */
    {
        /* Load geometry: */
        m_geometry = gEDataManager->selectorWindowGeometry(this);

        /* Restore geometry: */
        LogRel2(("GUI: UIVirtualBoxManager: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
                 m_geometry.x(), m_geometry.y(), m_geometry.width(), m_geometry.height()));
        restoreGeometry();
    }
}

void UIVirtualBoxManager::saveSettings()
{
    /* Save window geometry: */
    {
#ifdef VBOX_WS_MAC
        gEDataManager->setSelectorWindowGeometry(m_geometry, ::darwinIsWindowMaximized(this));
#else /* VBOX_WS_MAC */
        gEDataManager->setSelectorWindowGeometry(m_geometry, isMaximized());
#endif /* !VBOX_WS_MAC */
        LogRel2(("GUI: UIVirtualBoxManager: Geometry saved as: Origin=%dx%d, Size=%dx%d\n",
                 m_geometry.x(), m_geometry.y(), m_geometry.width(), m_geometry.height()));
    }
}

void UIVirtualBoxManager::cleanupConnections()
{
    /* Honestly we should disconnect everything here,
     * but for now it's enough to disconnect the most critical. */
    m_pWidget->disconnect(this);
}

void UIVirtualBoxManager::cleanupWidgets()
{
    /* Deconfigure central-widget: */
    setCentralWidget(0);
    /* Destroy central-widget: */
    delete m_pWidget;
    m_pWidget = 0;
}

void UIVirtualBoxManager::cleanupMenuBar()
{
#ifdef VBOX_WS_MAC
    /* Cleanup 'Window' menu: */
    UIWindowMenuManager::destroy();
#endif

    /* Destroy action-pool: */
    UIActionPool::destroy(m_pActionPool);
    m_pActionPool = 0;
}

void UIVirtualBoxManager::cleanup()
{
    /* Close the sub-dialogs first: */
    sltCloseVirtualMediumManagerWindow();
    sltCloseHostNetworkManagerWindow();
    sltCloseCloudProfileManagerWindow();

    /* Save settings: */
    saveSettings();

    /* Cleanup: */
    cleanupConnections();
    cleanupWidgets();
    cleanupMenuBar();
}

UIVirtualMachineItem *UIVirtualBoxManager::currentItem() const
{
    return m_pWidget->currentItem();
}

QList<UIVirtualMachineItem*> UIVirtualBoxManager::currentItems() const
{
    return m_pWidget->currentItems();
}

bool UIVirtualBoxManager::isGroupSavingInProgress() const
{
    return m_pWidget->isGroupSavingInProgress();
}

bool UIVirtualBoxManager::isAllItemsOfOneGroupSelected() const
{
    return m_pWidget->isAllItemsOfOneGroupSelected();
}

bool UIVirtualBoxManager::isSingleGroupSelected() const
{
    return m_pWidget->isSingleGroupSelected();
}

void UIVirtualBoxManager::performStartOrShowVirtualMachines(const QList<UIVirtualMachineItem*> &items, VBoxGlobal::LaunchMode enmLaunchMode)
{
    /* Do nothing while group saving is in progress: */
    if (isGroupSavingInProgress())
        return;

    /* Compose the list of startable items: */
    QStringList startableMachineNames;
    QList<UIVirtualMachineItem*> startableItems;
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (isAtLeastOneItemCanBeStarted(QList<UIVirtualMachineItem*>() << pItem))
        {
            startableItems << pItem;
            startableMachineNames << pItem->name();
        }
    }

    /* Initially we have start auto-confirmed: */
    bool fStartConfirmed = true;
    /* But if we have more than one item to start =>
     * We should still ask user for a confirmation: */
    if (startableItems.size() > 1)
        fStartConfirmed = msgCenter().confirmStartMultipleMachines(startableMachineNames.join(", "));

    /* For every item => check if it could be launched: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (   isAtLeastOneItemCanBeShown(QList<UIVirtualMachineItem*>() << pItem)
            || (   isAtLeastOneItemCanBeStarted(QList<UIVirtualMachineItem*>() << pItem)
                && fStartConfirmed))
        {
            /* Fetch item launch mode: */
            VBoxGlobal::LaunchMode enmItemLaunchMode = enmLaunchMode;
            if (enmItemLaunchMode == VBoxGlobal::LaunchMode_Invalid)
                enmItemLaunchMode = UIVirtualMachineItem::isItemRunningHeadless(pItem) ? VBoxGlobal::LaunchMode_Separate :
                                    qApp->keyboardModifiers() == Qt::ShiftModifier     ? VBoxGlobal::LaunchMode_Headless :
                                                                                         VBoxGlobal::LaunchMode_Default;

            /* Launch current VM: */
            CMachine machine = pItem->machine();
            vboxGlobal().launchMachine(machine, enmItemLaunchMode);
        }
    }
}

void UIVirtualBoxManager::updateActionsVisibility()
{
    /* Determine whether Machine or Group menu should be shown at all: */
    const bool fGlobalMenuShown  = m_pWidget->isGlobalItemSelected();
    const bool fGroupMenuShown   = m_pWidget->isGroupItemSelected()   &&  isSingleGroupSelected();
    const bool fMachineMenuShown = m_pWidget->isMachineItemSelected() && !isSingleGroupSelected();
    actionPool()->action(UIActionIndexST_M_Welcome)->setVisible(fGlobalMenuShown);
    actionPool()->action(UIActionIndexST_M_Group)->setVisible(fGroupMenuShown);
    actionPool()->action(UIActionIndexST_M_Machine)->setVisible(fMachineMenuShown);

    /* Determine whether Media menu should be visible: */
    const bool fMediumMenuShown = fGlobalMenuShown && m_pWidget->currentGlobalTool() == UIToolType_Media;
    actionPool()->action(UIActionIndexST_M_Medium)->setVisible(fMediumMenuShown);
    /* Determine whether Network menu should be visible: */
    const bool fNetworkMenuShown = fGlobalMenuShown && m_pWidget->currentGlobalTool() == UIToolType_Network;
    actionPool()->action(UIActionIndexST_M_Network)->setVisible(fNetworkMenuShown);
    /* Determine whether Cloud menu should be visible: */
    const bool fCloudMenuShown = fGlobalMenuShown && m_pWidget->currentGlobalTool() == UIToolType_Cloud;
    actionPool()->action(UIActionIndexST_M_Cloud)->setVisible(fCloudMenuShown);

    /* Determine whether Snapshots menu should be visible: */
    const bool fSnapshotMenuShown = (fMachineMenuShown || fGroupMenuShown) &&
                                    m_pWidget->currentMachineTool() == UIToolType_Snapshots;
    actionPool()->action(UIActionIndexST_M_Snapshot)->setVisible(fSnapshotMenuShown);
    /* Determine whether Logs menu should be visible: */
    const bool fLogViewerMenuShown = (fMachineMenuShown || fGroupMenuShown) &&
                                     m_pWidget->currentMachineTool() == UIToolType_Logs;
    actionPool()->action(UIActionIndex_M_Log)->setVisible(fLogViewerMenuShown);

    /* Hide action shortcuts: */
    if (!fGlobalMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexST_M_Welcome, false);
    if (!fGroupMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexST_M_Group, false);
    if (!fMachineMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexST_M_Machine, false);

    /* Show action shortcuts: */
    if (fGlobalMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexST_M_Welcome, true);
    if (fGroupMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexST_M_Group, true);
    if (fMachineMenuShown)
        actionPool()->setShortcutsVisible(UIActionIndexST_M_Machine, true);
}

void UIVirtualBoxManager::updateActionsAppearance()
{
    /* Get current items: */
    QList<UIVirtualMachineItem*> items = currentItems();

    /* Enable/disable File/Application actions: */
    actionPool()->action(UIActionIndex_M_Application_S_Preferences)->setEnabled(isActionEnabled(UIActionIndex_M_Application_S_Preferences, items));
    actionPool()->action(UIActionIndexST_M_File_S_ExportAppliance)->setEnabled(isActionEnabled(UIActionIndexST_M_File_S_ExportAppliance, items));
    actionPool()->action(UIActionIndexST_M_File_S_ImportAppliance)->setEnabled(isActionEnabled(UIActionIndexST_M_File_S_ImportAppliance, items));

    /* Enable/disable welcome actions: */
    actionPool()->action(UIActionIndexST_M_Welcome_S_Add)->setEnabled(isActionEnabled(UIActionIndexST_M_Welcome_S_Add, items));

    /* Enable/disable group actions: */
    actionPool()->action(UIActionIndexST_M_Group_S_Rename)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_Rename, items));
    actionPool()->action(UIActionIndexST_M_Group_S_Remove)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_Remove, items));
    actionPool()->action(UIActionIndexST_M_Group_T_Pause)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_T_Pause, items));
    actionPool()->action(UIActionIndexST_M_Group_S_Reset)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_Reset, items));
    actionPool()->action(UIActionIndexST_M_Group_S_Discard)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_Discard, items));
    actionPool()->action(UIActionIndexST_M_Group_S_ShowLogDialog)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_ShowLogDialog, items));
    actionPool()->action(UIActionIndexST_M_Group_S_Refresh)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_Refresh, items));
    actionPool()->action(UIActionIndexST_M_Group_S_ShowInFileManager)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_ShowInFileManager, items));
    actionPool()->action(UIActionIndexST_M_Group_S_CreateShortcut)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_CreateShortcut, items));
    actionPool()->action(UIActionIndexST_M_Group_S_Sort)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_S_Sort, items));

    /* Enable/disable machine actions: */
    actionPool()->action(UIActionIndexST_M_Machine_S_Settings)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_Settings, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_Clone)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_Clone, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_Move)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_Move, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_ExportToOCI)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_ExportToOCI, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_Remove)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_Remove, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_AddGroup)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_AddGroup, items));
    actionPool()->action(UIActionIndexST_M_Machine_T_Pause)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_T_Pause, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_Reset)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_Reset, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_Discard)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_Discard, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_ShowLogDialog)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_ShowLogDialog, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_Refresh)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_Refresh, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_ShowInFileManager)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_ShowInFileManager, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_CreateShortcut)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_CreateShortcut, items));
    actionPool()->action(UIActionIndexST_M_Machine_S_SortParent)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_S_SortParent, items));

    /* Enable/disable group-start-or-show actions: */
    actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_StartOrShow, items));
    actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow_S_StartNormal)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_StartOrShow_S_StartNormal, items));
    actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow_S_StartHeadless)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_StartOrShow_S_StartHeadless, items));
    actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow_S_StartDetachable)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_StartOrShow_S_StartDetachable, items));

    /* Enable/disable machine-start-or-show actions: */
    actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_StartOrShow, items));
    actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow_S_StartNormal)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_StartOrShow_S_StartNormal, items));
    actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow_S_StartHeadless)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_StartOrShow_S_StartHeadless, items));
    actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow_S_StartDetachable)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_StartOrShow_S_StartDetachable, items));

    /* Enable/disable group-close actions: */
    actionPool()->action(UIActionIndexST_M_Group_M_Close)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_Close, items));
    actionPool()->action(UIActionIndexST_M_Group_M_Close_S_Detach)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_Close_S_Detach, items));
    actionPool()->action(UIActionIndexST_M_Group_M_Close_S_SaveState)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_Close_S_SaveState, items));
    actionPool()->action(UIActionIndexST_M_Group_M_Close_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_Close_S_Shutdown, items));
    actionPool()->action(UIActionIndexST_M_Group_M_Close_S_PowerOff)->setEnabled(isActionEnabled(UIActionIndexST_M_Group_M_Close_S_PowerOff, items));

    /* Enable/disable machine-close actions: */
    actionPool()->action(UIActionIndexST_M_Machine_M_Close)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_Close, items));
    actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_Detach)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_Detach, items));
    actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_SaveState)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_SaveState, items));
    actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_Shutdown)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_Shutdown, items));
    actionPool()->action(UIActionIndexST_M_Machine_M_Close_S_PowerOff)->setEnabled(isActionEnabled(UIActionIndexST_M_Machine_M_Close_S_PowerOff, items));

    /* Get current item: */
    UIVirtualMachineItem *pItem = currentItem();

    /* Start/Show action is deremined by 1st item: */
    if (pItem && pItem->accessible())
    {
        actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow)->toActionPolymorphicMenu()->setState(UIVirtualMachineItem::isItemPoweredOff(pItem) ? 0 : 1);
        actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow)->toActionPolymorphicMenu()->setState(UIVirtualMachineItem::isItemPoweredOff(pItem) ? 0 : 1);
        /// @todo Hmm, fix it?
//        QToolButton *pButton = qobject_cast<QToolButton*>(m_pToolBar->widgetForAction(actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow)));
//        if (pButton)
//            pButton->setPopupMode(UIVirtualMachineItem::isItemPoweredOff(pItem) ? QToolButton::MenuButtonPopup : QToolButton::DelayedPopup);
    }
    else
    {
        actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow)->toActionPolymorphicMenu()->setState(0);
        actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow)->toActionPolymorphicMenu()->setState(0);
        /// @todo Hmm, fix it?
//        QToolButton *pButton = qobject_cast<QToolButton*>(m_pToolBar->widgetForAction(actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow)));
//        if (pButton)
//            pButton->setPopupMode(UIVirtualMachineItem::isItemPoweredOff(pItem) ? QToolButton::MenuButtonPopup : QToolButton::DelayedPopup);
    }

    /* Pause/Resume action is deremined by 1st started item: */
    UIVirtualMachineItem *pFirstStartedAction = 0;
    foreach (UIVirtualMachineItem *pSelectedItem, items)
    {
        if (UIVirtualMachineItem::isItemStarted(pSelectedItem))
        {
            pFirstStartedAction = pSelectedItem;
            break;
        }
    }
    /* Update the group Pause/Resume action appearance: */
    actionPool()->action(UIActionIndexST_M_Group_T_Pause)->blockSignals(true);
    actionPool()->action(UIActionIndexST_M_Group_T_Pause)->setChecked(pFirstStartedAction && UIVirtualMachineItem::isItemPaused(pFirstStartedAction));
    actionPool()->action(UIActionIndexST_M_Group_T_Pause)->retranslateUi();
    actionPool()->action(UIActionIndexST_M_Group_T_Pause)->blockSignals(false);
    /* Update the machine Pause/Resume action appearance: */
    actionPool()->action(UIActionIndexST_M_Machine_T_Pause)->blockSignals(true);
    actionPool()->action(UIActionIndexST_M_Machine_T_Pause)->setChecked(pFirstStartedAction && UIVirtualMachineItem::isItemPaused(pFirstStartedAction));
    actionPool()->action(UIActionIndexST_M_Machine_T_Pause)->retranslateUi();
    actionPool()->action(UIActionIndexST_M_Machine_T_Pause)->blockSignals(false);

    /* Update action toggle states: */
    if (m_pWidget)
    {
        switch (m_pWidget->currentMachineTool())
        {
            case UIToolType_Details:
            {
                actionPool()->action(UIActionIndexST_M_Group_M_Tools_T_Details)->setChecked(true);
                actionPool()->action(UIActionIndexST_M_Machine_M_Tools_T_Details)->setChecked(true);
                break;
            }
            case UIToolType_Snapshots:
            {
                actionPool()->action(UIActionIndexST_M_Group_M_Tools_T_Snapshots)->setChecked(true);
                actionPool()->action(UIActionIndexST_M_Machine_M_Tools_T_Snapshots)->setChecked(true);
                break;
            }
            case UIToolType_Logs:
            {
                actionPool()->action(UIActionIndexST_M_Group_M_Tools_T_Logs)->setChecked(true);
                actionPool()->action(UIActionIndexST_M_Machine_M_Tools_T_Logs)->setChecked(true);
                break;
            }
            default:
                break;
        }
    }
}

bool UIVirtualBoxManager::isActionEnabled(int iActionIndex, const QList<UIVirtualMachineItem*> &items)
{
    /* For known *global* action types: */
    switch (iActionIndex)
    {
        case UIActionIndex_M_Application_S_Preferences:
        case UIActionIndexST_M_File_S_ExportAppliance:
        case UIActionIndexST_M_File_S_ImportAppliance:
        case UIActionIndexST_M_Welcome_S_Add:
        {
            return !actionPool()->action(iActionIndex)->property("opened").toBool();
        }
        default:
            break;
    }

    /* No *machine* actions enabled for empty item list: */
    if (items.isEmpty())
        return false;

    /* Get first item: */
    UIVirtualMachineItem *pItem = items.first();

    /* For known *machine* action types: */
    switch (iActionIndex)
    {
        case UIActionIndexST_M_Group_S_Rename:
        case UIActionIndexST_M_Group_S_Remove:
        {
            return !isGroupSavingInProgress() &&
                   isItemsPoweredOff(items);
        }
        case UIActionIndexST_M_Group_S_Sort:
        {
            return !isGroupSavingInProgress() &&
                   isSingleGroupSelected();
        }
        case UIActionIndexST_M_Machine_S_Settings:
        {
            return !actionPool()->action(iActionIndex)->property("opened").toBool() &&
                   !isGroupSavingInProgress() &&
                   items.size() == 1 &&
                   pItem->configurationAccessLevel() != ConfigurationAccessLevel_Null &&
                   (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                    m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexST_M_Machine_S_Clone:
        case UIActionIndexST_M_Machine_S_Move:
        {
            return !isGroupSavingInProgress() &&
                   items.size() == 1 &&
                   UIVirtualMachineItem::isItemEditable(pItem);
        }
        case UIActionIndexST_M_Machine_S_ExportToOCI:
        {
            return !actionPool()->action(iActionIndex)->property("opened").toBool() &&
                   items.size() == 1;
        }
        case UIActionIndexST_M_Machine_S_Remove:
        {
            return !isGroupSavingInProgress() &&
                   isAtLeastOneItemRemovable(items);
        }
        case UIActionIndexST_M_Machine_S_AddGroup:
        {
            return !isGroupSavingInProgress() &&
                   !isAllItemsOfOneGroupSelected() &&
                   isItemsPoweredOff(items);
        }
        case UIActionIndexST_M_Group_M_StartOrShow:
        case UIActionIndexST_M_Group_M_StartOrShow_S_StartNormal:
        case UIActionIndexST_M_Group_M_StartOrShow_S_StartHeadless:
        case UIActionIndexST_M_Group_M_StartOrShow_S_StartDetachable:
        case UIActionIndexST_M_Machine_M_StartOrShow:
        case UIActionIndexST_M_Machine_M_StartOrShow_S_StartNormal:
        case UIActionIndexST_M_Machine_M_StartOrShow_S_StartHeadless:
        case UIActionIndexST_M_Machine_M_StartOrShow_S_StartDetachable:
        {
            return !isGroupSavingInProgress() &&
                   isAtLeastOneItemCanBeStartedOrShown(items) &&
                    (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                     m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexST_M_Group_S_Discard:
        case UIActionIndexST_M_Machine_S_Discard:
        {
            return !isGroupSavingInProgress() &&
                   isAtLeastOneItemDiscardable(items) &&
                    (m_pWidget->currentMachineTool() != UIToolType_Snapshots ||
                     m_pWidget->isCurrentStateItemSelected());
        }
        case UIActionIndexST_M_Group_S_ShowLogDialog:
        case UIActionIndexST_M_Machine_S_ShowLogDialog:
        {
            return isAtLeastOneItemAccessible(items);
        }
        case UIActionIndexST_M_Group_T_Pause:
        case UIActionIndexST_M_Machine_T_Pause:
        {
            return isAtLeastOneItemStarted(items);
        }
        case UIActionIndexST_M_Group_S_Reset:
        case UIActionIndexST_M_Machine_S_Reset:
        {
            return isAtLeastOneItemRunning(items);
        }
        case UIActionIndexST_M_Group_S_Refresh:
        case UIActionIndexST_M_Machine_S_Refresh:
        {
            return isAtLeastOneItemInaccessible(items);
        }
        case UIActionIndexST_M_Group_S_ShowInFileManager:
        case UIActionIndexST_M_Machine_S_ShowInFileManager:
        {
            return isAtLeastOneItemAccessible(items);
        }
        case UIActionIndexST_M_Machine_S_SortParent:
        {
            return !isGroupSavingInProgress();
        }
        case UIActionIndexST_M_Group_S_CreateShortcut:
        case UIActionIndexST_M_Machine_S_CreateShortcut:
        {
            return isAtLeastOneItemSupportsShortcuts(items);
        }
        case UIActionIndexST_M_Group_M_Close:
        case UIActionIndexST_M_Machine_M_Close:
        {
            return isAtLeastOneItemStarted(items);
        }
        case UIActionIndexST_M_Group_M_Close_S_Detach:
        case UIActionIndexST_M_Machine_M_Close_S_Detach:
        {
            return isActionEnabled(UIActionIndexST_M_Machine_M_Close, items);
        }
        case UIActionIndexST_M_Group_M_Close_S_SaveState:
        case UIActionIndexST_M_Machine_M_Close_S_SaveState:
        {
            return isActionEnabled(UIActionIndexST_M_Machine_M_Close, items);
        }
        case UIActionIndexST_M_Group_M_Close_S_Shutdown:
        case UIActionIndexST_M_Machine_M_Close_S_Shutdown:
        {
            return isActionEnabled(UIActionIndexST_M_Machine_M_Close, items) &&
                   isAtLeastOneItemAbleToShutdown(items);
        }
        case UIActionIndexST_M_Group_M_Close_S_PowerOff:
        case UIActionIndexST_M_Machine_M_Close_S_PowerOff:
        {
            return isActionEnabled(UIActionIndexST_M_Machine_M_Close, items);
        }
        default:
            break;
    }

    /* Unknown actions are disabled: */
    return false;
}

/* static */
bool UIVirtualBoxManager::isItemsPoweredOff(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (!UIVirtualMachineItem::isItemPoweredOff(pItem))
            return false;
    return true;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemAbleToShutdown(const QList<UIVirtualMachineItem*> &items)
{
    /* Enumerate all the passed items: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Skip non-running machines: */
        if (!UIVirtualMachineItem::isItemRunning(pItem))
            continue;
        /* Skip session failures: */
        CSession session = vboxGlobal().openExistingSession(pItem->id());
        if (session.isNull())
            continue;
        /* Skip console failures: */
        CConsole console = session.GetConsole();
        if (console.isNull())
        {
            /* Do not forget to release machine: */
            session.UnlockMachine();
            continue;
        }
        /* Is the guest entered ACPI mode? */
        bool fGuestEnteredACPIMode = console.GetGuestEnteredACPIMode();
        /* Do not forget to release machine: */
        session.UnlockMachine();
        /* True if the guest entered ACPI mode: */
        if (fGuestEnteredACPIMode)
            return true;
    }
    /* False by default: */
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemSupportsShortcuts(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (   pItem->accessible()
#ifdef VBOX_WS_MAC
            /* On Mac OS X this are real alias files, which don't work with the old legacy xml files. */
            && pItem->settingsFile().endsWith(".vbox", Qt::CaseInsensitive)
#endif
            )
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemAccessible(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (pItem->accessible())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemInaccessible(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (!pItem->accessible())
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemRemovable(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (!pItem->accessible() || UIVirtualMachineItem::isItemEditable(pItem))
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemCanBeStarted(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (UIVirtualMachineItem::isItemPoweredOff(pItem) && UIVirtualMachineItem::isItemEditable(pItem))
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemCanBeShown(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if (UIVirtualMachineItem::isItemStarted(pItem) && (pItem->canSwitchTo() || UIVirtualMachineItem::isItemRunningHeadless(pItem)))
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemCanBeStartedOrShown(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
    {
        if ((UIVirtualMachineItem::isItemPoweredOff(pItem) && UIVirtualMachineItem::isItemEditable(pItem)) ||
            (UIVirtualMachineItem::isItemStarted(pItem) && (pItem->canSwitchTo() || UIVirtualMachineItem::isItemRunningHeadless(pItem))))
            return true;
    }
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemDiscardable(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (UIVirtualMachineItem::isItemSaved(pItem) && UIVirtualMachineItem::isItemEditable(pItem))
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemStarted(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (UIVirtualMachineItem::isItemStarted(pItem))
            return true;
    return false;
}

/* static */
bool UIVirtualBoxManager::isAtLeastOneItemRunning(const QList<UIVirtualMachineItem*> &items)
{
    foreach (UIVirtualMachineItem *pItem, items)
        if (UIVirtualMachineItem::isItemRunning(pItem))
            return true;
    return false;
}
