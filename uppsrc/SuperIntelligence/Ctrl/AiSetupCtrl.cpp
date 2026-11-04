#include "Ctrl.h"

NAMESPACE_UPP

AiSetupDlg::AiSetupDlg() {
	CtrlLayoutOKCancel(*this, "AI Providers & Account Settings");
	Sizeable().Zoomable();

	providers.AddColumn("Name");
	providers.AddColumn("Type");
	providers.AddColumn("Endpoint");
	providers.AddColumn("Priority");
	providers.ColumnWidths("120 140 180 60");
	providers.WhenCursor = THISBACK(OnProviderSelect);

	for (int i = 0; i < AiServiceProvider::TYPE_COUNT; i++)
		type.Add(i, AiServiceProvider::GetTypeString(i));

	add.WhenAction = THISBACK(AddProvider);
	remove.WhenAction = THISBACK(RemoveProvider);

	name.WhenAction = THISBACK(ApplySelected);
	type.WhenAction = THISBACK(ApplySelected);
	url.WhenAction = THISBACK(ApplySelected);
	token.WhenAction = THISBACK(ApplySelected);
	priority.WhenAction = THISBACK(ApplySelected);

	ok.WhenAction = THISBACK(SaveAndClose);

	SyncProviders();
	if (providers.GetCount() > 0)
		providers.SetCursor(0);
}

void AiSetupDlg::SyncProviders() {
	int cur = providers.GetCursor();
	providers.Clear();
	auto& mgr = AiManager();
	for (int i = 0; i < mgr.GetCount(); i++) {
		const auto& p = mgr[i];
		providers.Add(p.name, p.GetTypeString(), p.url, p.priority);
	}
	if (cur >= 0 && cur < providers.GetCount())
		providers.SetCursor(cur);
}

void AiSetupDlg::OnProviderSelect() {
	int c = providers.GetCursor();
	auto& mgr = AiManager();
	if (c >= 0 && c < mgr.GetCount()) {
		const auto& p = mgr[c];
		name.SetData(p.name);
		type.SetData((int)p.type);
		url.SetData(p.url);
		token.SetData(p.token);
		priority.SetData(p.priority);
	}
	else {
		name.Clear();
		type.Clear();
		url.Clear();
		token.Clear();
		priority.Clear();
	}
}

void AiSetupDlg::AddProvider() {
	auto& mgr = AiManager();
	auto& p = mgr.Add();
	p.name = "New Provider";
	p.type = AiServiceProvider::OPENAI;
	p.url = "https://api.openai.com/v1";
	p.priority = 0;
	SyncProviders();
	providers.SetCursor(mgr.GetCount() - 1);
}

void AiSetupDlg::RemoveProvider() {
	int c = providers.GetCursor();
	auto& mgr = AiManager();
	if (c >= 0 && c < mgr.GetCount()) {
		mgr.Remove(c);
		SyncProviders();
		if (c >= mgr.GetCount())
			c = mgr.GetCount() - 1;
		if (c >= 0)
			providers.SetCursor(c);
	}
}

void AiSetupDlg::ApplySelected() {
	int c = providers.GetCursor();
	auto& mgr = AiManager();
	if (c >= 0 && c < mgr.GetCount()) {
		auto& p = mgr[c];
		p.name = name.GetData();
		p.type = (AiServiceProvider::Type)(int)type.GetData();
		p.url = url.GetData();
		p.token = token.GetData();
		p.priority = (int)priority.GetData();
		providers.Set(c, 0, p.name);
		providers.Set(c, 1, p.GetTypeString());
		providers.Set(c, 2, p.url);
		providers.Set(c, 3, p.priority);
	}
}

void AiSetupDlg::SaveAndClose() {
	ApplySelected();
	AiManager().Save();
	Break(IDOK);
}

void RunAiSetupModal() {
	AiSetupDlg dlg;
	dlg.Execute();
}

END_UPP_NAMESPACE
