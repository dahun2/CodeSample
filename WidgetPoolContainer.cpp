// Fill out your copyright notice in the Description page of Project Settings.


#include "WidgetPoolContainer.h"

UUserWidget* FWidgetPoolContainer::GetChild()
{
	if (DeActivatedChildList.Num() > 0)
	{
		return DeActivatedChildList.Pop();
	}
	else
	{
		return CreateNewChild();
	}
}

UUserWidget* FWidgetPoolContainer::AddToPanel()
{
	UUserWidget* OutChild = GetChild();
	if (IsValid(OutChild) == false) return nullptr;

	OutChild->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Panel->AddChild(OutChild);
	ActivatedChildList.Emplace(OutChild);
	return OutChild;
}

void FWidgetPoolContainer::RemoveAtFromPanel(int InIndex)
{
	if (ActivatedChildList.IsValidIndex(InIndex) == false) return;

	ActivatedChildList[InIndex]->RemoveFromParent();

	DeActivatedChildList.Emplace(ActivatedChildList[InIndex]);
	ActivatedChildList.RemoveAt(InIndex);
}

void FWidgetPoolContainer::RemoveFromPanel(UUserWidget* InChild)
{
	int Index = ActivatedChildList.Find(InChild);
	if (Index == INDEX_NONE) return;

	RemoveAtFromPanel(Index);
}

void FWidgetPoolContainer::RemoveAllFromPanel()
{
	for (UUserWidget* InChild : ActivatedChildList)
	{
		InChild->RemoveFromParent();
		DeActivatedChildList.Emplace(InChild);
	}

	ActivatedChildList.Empty();
}


void FWidgetPoolContainer::Clear()
{
	if (IsValid(Panel) == false) return;

	Panel->ClearChildren();

	DeActivatedChildList.Empty();
	ActivatedChildList.Empty();
}

UUserWidget* FWidgetPoolContainer::GetActivatedChildAtIndex(int InIndex)
{
	if (ActivatedChildList.IsValidIndex(InIndex) == false) return nullptr;

	return ActivatedChildList[InIndex];
}

void FWidgetPoolContainer::SetInitFunc(TFunction<void(TArray<UUserWidget*> OutChildList)> InInitFunc)
{
	if (InInitFunc)
	{
		InitFunc = InInitFunc;
	}
}

UUserWidget* FWidgetPoolContainer::FindActivatedChildByPredicate(TFunction<UUserWidget*(UUserWidget* ActivatedChild)> InPred)
{
	if (InPred)
	{
		UUserWidget** FoundWidget = ActivatedChildList.FindByPredicate(InPred);
		if (FoundWidget != nullptr)
		{
			return *FoundWidget;
		}
	}

	return nullptr;
}

UUserWidget* FWidgetPoolContainer::CreateNewChild()
{
	if (IsValid(ChildClass.Get()) == false) return nullptr;
	if (IsValid(Panel) == false) return nullptr;

	UUserWidget* NewChild = CreateWidget(Panel->GetWorld(), ChildClass);
	if (NewChild == nullptr) return nullptr;

	return NewChild;
}