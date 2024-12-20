// Authors: xbuten00(Pavlo Butenko), xkolia00(Nikita Koliada)

#include "simlib.h"
#include <unistd.h>

#define seconds(sec) sec
#define minutes(min) seconds(min * 60)
#define hours(h) minutes(h * 60)
#define Norm(mean, sigma) std::max(0.0, Normal(mean, sigma))
#define Exp(mean) std::max(0.0, Exponential(mean))
 
Facility VoucherMachine("voucher machine");
Facility Reception("reception");
Queue ReceptionQueue("reception");
Facility ComplaintDesk("complaint desk");
Store* Consultants;
Queue ConsultantsQueue("consultants");
Store* CoffeeMachines;

int consultant_services = 0;
int successful_consultant_services = 0;
int documents_invalid_count = 0;
int total_customer_count = 0;
int cash_replenishment_user_left = 0;

int loan_count = 0;
int investment_advice_count = 0;
int fraud_processing_count = 0;
int registration_count = 0;

class ATMs
{
public:
    Store _Store;
    Queue _Queue;
    bool AreProcessed;
    ATMs(int atmCount) : _Store("atms", atmCount), _Queue("atms")
    {
        AreProcessed = false;
    }
};

ATMs* ATMsQueue;

class Visitor : public Process
{
public:
    Visitor() : goesForCoffee(false){}
    void Behavior() override;
    void setGoForCoffee() { goesForCoffee = true; }

private:
    bool goesForCoffee;
    void WentForCoffee();
    void MakeAComplaint(double percent);
    void CallNextCustomer()
    {
        if (!ConsultantsQueue.Empty())
        {
            ConsultantsQueue.GetFirst()->Activate();
        }
    }
};

class VisitorWantsCoffee : public Process
{
private:
    Visitor *visitor;
    int maxAwaitTime;

public:
    VisitorWantsCoffee(Visitor *visitor, int maxAwaitTime)
    {
        this->visitor = visitor;
        this->maxAwaitTime = maxAwaitTime;
    }

    void Behavior() override
    {
        Wait(maxAwaitTime);
        visitor->Activate();
        visitor->setGoForCoffee();
    }
};

class VisitorGenerator : public Event
{
public:
    VisitorGenerator(double expTime)
    {
        this->ExpTime = expTime;
    }

private:
    double ExpTime;
    void Behavior()
    {
        (new Visitor())->Activate();
        total_customer_count++;
        this->Activate(Time + Exp(ExpTime));
    }
};
class CashGuys : public Process
{
private:
    int frequencyArrival;

public:
    bool atmsAreProcessing = false;
    CashGuys(int frequencyArrival)
    {
        this->frequencyArrival = frequencyArrival;
    }
    void Behavior() override
    {
        while (true)
        {
            Wait(Exp(frequencyArrival));
            auto atmCount = ATMsQueue->_Store.Capacity();
            auto takenATMs = 0;
            Priority = 1;
            while (takenATMs < atmCount)
            {
                if(ATMsQueue->_Store.Full()){
                    ATMsQueue->_Queue.InsFirst(this);
                    Passivate();
                }
                Enter(ATMsQueue->_Store);
                takenATMs++;
            }

            ATMsQueue->AreProcessed = true;
            Wait(Norm(minutes(30), minutes(5)));
            Leave(ATMsQueue->_Store, atmCount);
            ATMsQueue->AreProcessed = false;
        }
    }
};

void Visitor::Behavior()
{
    auto entryTime = Time;
    bool needsAnotherService = true;
    while (needsAnotherService)
    {
        needsAnotherService = false;
        auto serviceType = Random();
        if (serviceType < 0.2)
        {
            // Reception processing
            while (Reception.Busy() || !ReceptionQueue.Empty())
            {
                Into(ReceptionQueue);
                auto wantsCoffee = new VisitorWantsCoffee(this, minutes(4));
                wantsCoffee->Activate();
                Passivate();

                if (goesForCoffee)
                {
                    Out();
                    WentForCoffee();
                    goesForCoffee = false;
                }
                else
                {
                    wantsCoffee->Cancel();
                    break;
                }
            }


            Seize(Reception);
            Wait(Norm(minutes(5), minutes(2)));
            Release(Reception);
            if (!ReceptionQueue.Empty())
            {
                ReceptionQueue.GetFirst()->Activate();
            }

            if (Random() < 0.8)
            {
                needsAnotherService = true;
            }
        }
        else if (serviceType >= 0.2 && serviceType < 0.6)
        {
            // ATMs
            if (ATMsQueue->AreProcessed)
            {
                cash_replenishment_user_left++;
                break;
            }

            if (ATMsQueue->_Store.Full())
            {
                Into(ATMsQueue->_Queue);
                Passivate();
            }

            if (ATMsQueue->AreProcessed)
            {
                cash_replenishment_user_left++;
                break;
            }

            Enter(ATMsQueue->_Store);
            Wait(Exp(minutes(6)));
            Leave(ATMsQueue->_Store);

            if (!ATMsQueue->_Queue.Empty())
            {
                ATMsQueue->_Queue.GetFirst()->Activate();
            }
        }
        else if (serviceType >= 0.6 && serviceType < 0.9)
        {
            // Consultants
            auto isPremium = Random() < 0.15;
            if (!isPremium)
            {
                Seize(VoucherMachine);
                Wait(Norm(seconds(30), seconds(10)));
                Release(VoucherMachine);
            }

            while (Consultants->Full() || !ConsultantsQueue.Empty())
            {
                // function to check if the visitor goes for coffee
                if (isPremium)
                {
                    Priority = 1;
                }
                Into(ConsultantsQueue);
                auto wantsCoffee = new VisitorWantsCoffee(this, minutes(8));
                wantsCoffee->Activate();
                Passivate();

                if (goesForCoffee)
                {
                    Out();
                    WentForCoffee();
                    goesForCoffee = false;
                }
                else
                {
                    wantsCoffee->Cancel();
                    break;
                }
            }
            

            Enter(*Consultants);
            consultant_services++;

            Priority = 0;
            Wait(Exp(minutes(1)));

            // documents check
            auto isDocumentsValid = Random();
            if (isDocumentsValid < 0.25)
            {
                // No
                Leave(*Consultants);
                documents_invalid_count++;
                CallNextCustomer();
                // Make a complaint
                MakeAComplaint(0.75);
            }
            else
            {
                // Yes
                auto consulatationProccess = Random();
                if (consulatationProccess < 0.5)
                {
                    // Registration
                    Wait(Norm(minutes(20), minutes(5)));
                    Leave(*Consultants);
                    successful_consultant_services++;
                    registration_count++;
                    CallNextCustomer();
                    // Could continue ?????
                }
                else if (consulatationProccess >= 0.5 && consulatationProccess < 0.7)
                {
                    // Investment plan
                    Wait(Norm(minutes(40), minutes(10)));
                    Leave(*Consultants);
                    successful_consultant_services++;
                    investment_advice_count++;
                    CallNextCustomer();
                    // Could continue ?????
                }
                else if (consulatationProccess >= 0.7 && consulatationProccess < 0.8)
                {
                    // Fraud investigation
                    Wait(Norm(minutes(10), minutes(2)));
                    auto isSolutionFound = Random();
                    Leave(*Consultants);
                    CallNextCustomer();
                    if (isSolutionFound < 0.8)
                    {
                        // No
                        MakeAComplaint(0.8);
                    }
                    else{
                        successful_consultant_services++;
                        fraud_processing_count++;
                    }
                }
                else
                {
                    // Taking a loan Background check
                    auto isBackgroundCheckValid = Random();
                    if (isBackgroundCheckValid < 0.8)
                    {
                        // Yes
                        Wait(Norm(minutes(30), minutes(5)));
                        Leave(*Consultants);
                        successful_consultant_services++;
                        loan_count++;
                        CallNextCustomer();
                    }
                    else
                    {
                        // No
                        Leave(*Consultants);
                        CallNextCustomer();
                        MakeAComplaint(0.4);
                    }
                }
            }
        }
        else
        {
            Priority = -1;
            MakeAComplaint(1);
        }
    }
};

void Visitor::WentForCoffee()
{
    Enter(*CoffeeMachines);
    Wait(minutes(1));
    Leave(*CoffeeMachines);
}

void Visitor::MakeAComplaint(double percent)
{
    auto wantsMakeComplaint = Random();
    if (wantsMakeComplaint < percent)
    {
        Seize(ComplaintDesk);
        Wait(minutes(1));
        Release(ComplaintDesk);
    }
}

int main(int argc, char** argv)
{
    int opt;
    int coffeeMachineCount = 3;
    int atmsCount = 3;
    int consultantCount = 4;

    while ((opt = getopt(argc, argv, "c:a:s:")) != -1) {
        switch (opt) {
            case 'c':
                coffeeMachineCount = std::stoi(optarg);
                break;
            case 's':
                consultantCount = std::stoi(optarg);
                break;
            case 'a':
                atmsCount = std::stoi(optarg);
                break;
        }
    }

    ATMsQueue = new ATMs(atmsCount);
    CoffeeMachines = new Store("coffee machines", coffeeMachineCount);
    Consultants = new Store("consultants", consultantCount);

    RandomSeed(time(NULL));
    SetOutput("bank.out");
    Init(0, hours(9));
    (new VisitorGenerator(minutes(5)))->Activate();
    (new CashGuys(hours(6)))->Activate();
    Run();

    CoffeeMachines->Output();
    ConsultantsQueue.Output();
    ReceptionQueue.Output();
    ComplaintDesk.Output();
    VoucherMachine.Output();
    Reception.Output();
    Consultants->Output();
    ATMsQueue->_Store.Output();
    ATMsQueue->_Queue.Output();

    Print("Total visitors: %d\n", total_customer_count);
    Print("Consultation success rate: %.2f%%\n", successful_consultant_services / float(consultant_services) * 100);
    Print("Invalid documents rate: %.2f%%\n", documents_invalid_count / float(consultant_services) * 100);
    Print("Count of visitors left due to atm replenishment: %d\n", cash_replenishment_user_left);
    Print("Taken loan count: %d\n", loan_count);
    Print("Invetsment services count: %d\n", investment_advice_count);
    Print("Successfully processed fraud count: %d\n", fraud_processing_count);
    Print("Registration count: %d\n", registration_count);

    delete ATMsQueue;
    delete CoffeeMachines;
    delete Consultants;

    return 0;
}