// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "WidgetPoolContainer.generated.h"


USTRUCT()
struct FWidgetPoolContainer
{
	GENERATED_BODY()

public:
	FWidgetPoolContainer() {}

	/** Init */
	template<typename T>
	void InitWidgetPoolContainer(UPanelWidget* InPanel, TSubclassOf<T> InChildClass)
	{
		InitWidgetPoolContainer(InPanel);
		ChildClass = InChildClass;
	}

	void InitWidgetPoolContainer(UPanelWidget* InPanel)
	{
		Panel = InPanel;
		Panel->ClearChildren();
	}

	/** Create child */
	UUserWidget* GetChild();

	/** Create child & Add to panel */
	template<typename T = UUserWidget>
	T* AddToPanel()
	{
		return Cast<T>(AddToPanel());
	}
	UUserWidget* AddToPanel();
	
	template<typename T>
	TArray<T*> AddToPanelCount(int InCount, TFunction<void(TArray<T*>& OutChildList)> InInitFunc = nullptr)
	{
		TArray<T*> OutChildList = {};
		for (int Index = 0; Index < InCount; Index++)
		{
			T* Child = AddToPanel<T>();
			if (IsValid(Child) == false) continue;
			OutChildList.Emplace(Child);
		}

		if (InInitFunc)
		{
			InInitFunc(OutChildList);
		}

		return OutChildList;
	}

	TArray<UUserWidget*> AddToPanelCount(int InCount, TFunction<void(TArray<UUserWidget*>& OutChildList)> InInitFunc = nullptr)
	{
		return AddToPanelCount<UUserWidget>(InCount, InInitFunc);
	}

	/** Remove from panel */
	void RemoveAtFromPanel(int InIndex);
	void RemoveFromPanel(UUserWidget* InChild);
	void RemoveAllFromPanel();

	/** Clear */
	void Clear();

	/** Get, Set */
	inline TArray<UUserWidget*>& GetActivatedChildList() { return ActivatedChildList; }
	UUserWidget* GetActivatedChildAtIndex(int InIndex);

	void SetInitFunc(TFunction<void(TArray<UUserWidget*> OutChildList)> InInitFunc);

public:
	UUserWidget* FindActivatedChildByPredicate(TFunction<UUserWidget*(UUserWidget* ActivatedChild)> InPred);

private:
	UUserWidget* CreateNewChild();

private:
	UPROPERTY()
	UPanelWidget* Panel = nullptr;

	UPROPERTY(EditAnywhere)
	TSubclassOf<UUserWidget> ChildClass;

	UPROPERTY()
	TArray<UUserWidget*> ActivatedChildList;

	UPROPERTY()
	TArray<UUserWidget*> DeActivatedChildList;

	TFunction<void(TArray<UUserWidget*> OutChildList)> InitFunc;
};
