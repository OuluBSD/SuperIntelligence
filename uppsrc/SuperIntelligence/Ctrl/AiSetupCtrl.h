#ifndef _SuperIntelligence_Ctrl_AiSetupCtrl_h_
#define _SuperIntelligence_Ctrl_AiSetupCtrl_h_

#include <CtrlLib/CtrlLib.h>
#include <SuperIntelligence/Provider/Provider.h>

NAMESPACE_UPP

class AiSetupDlg : public WithSetupAiLayout<TopWindow> {
public:
	typedef AiSetupDlg CLASSNAME;

	AiSetupDlg();
	void SyncProviders();
	void OnProviderSelect();
	void AddProvider();
	void RemoveProvider();
	void ApplySelected();
	void SaveAndClose();
};

void RunAiSetupModal();

END_UPP_NAMESPACE

#endif
