//
// Scheduler.cpp
// CloudSim
//
// Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//
// round robin solution
#include "Scheduler.hpp"
#include <cassert>
#include <queue>
#include <algorithm>
#include <climits>
#include <random>
static bool migrating = false;
static unsigned active_machines = 4;
// Keep track of how much memory is used on each machine
#include <map>
class PendingTask
{
public:
    TaskId_t task;
    MachineId_t machine;
    PendingTask(TaskId_t _task, MachineId_t _machine) : task(_task),
                                                        machine(_machine) {}
};
class MachineAndVms
{
public:
    MachineId_t machine;
    VMId_t vms[4];
    MachineAndVms(MachineId_t _machine, VMId_t vm)
        : machine(_machine) // <-- FIXED (removed stray comma)
    {
        for (int i = 0; i < 4; i++)
        {
            vms[i] = INT_MAX;
        }
        if (vm != INT_MAX)
        {
            vms[VM_GetInfo(vm).vm_type] = vm;
        }
    }
};
static map<TaskId_t, MachineId_t> task_to_machine;
static std::vector<PendingTask *> pending_tasks;
static std::vector<MachineId_t> pendingMachines;
static std::vector<MachineId_t> armMachines;
static std::vector<MachineId_t> powerMachines;
static std::vector<MachineId_t> riscvMachines;
static std::vector<MachineId_t> x86Machines;
static map<MachineId_t, MachineAndVms *> activeMachinesAndVms;
unsigned int nextArmMachine = 0;
unsigned int nextPowerMachine = 0;
unsigned int nextx86Machine = 0;
unsigned int nextRiscVMachine = 0;
// have to make sure vm is to an accurate
void Scheduler::Init()
{
    SimOutput("Scheduler::Init(): Total number of machines is " +
                  to_string(Machine_GetTotal()),
              3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    // active_machines = Machine_GetTotal() /2;
    // create initial machines
    for (unsigned int i = 0; i < Machine_GetTotal(); i++)
    {
        machines.push_back(MachineId_t(i));
        if (Machine_GetCPUType(MachineId_t(i)) == ARM)
        {
            armMachines.push_back(MachineId_t(i));
        }
        else if (Machine_GetCPUType(MachineId_t(i)) == POWER)
        {
            powerMachines.push_back(MachineId_t(i));
        }
        else if (Machine_GetCPUType(MachineId_t(i)) == RISCV)
        {
            riscvMachines.push_back(MachineId_t(i));
        }
        else if (Machine_GetCPUType(MachineId_t(i)) == X86)
        {
            x86Machines.push_back(MachineId_t(i));
        }
    }
    for (unsigned int i = 0; i < armMachines.size() / 8; i++)
    {
        VMId_t vm = VM_Create(LINUX, ARM);
        VM_Attach(vm, armMachines[i]);
        vms.push_back(vm);
        MachineAndVms *newMachine = new MachineAndVms(armMachines[i], vm);
        activeMachinesAndVms[armMachines[i]] = newMachine;
    }
    for (unsigned int i = 0; i < powerMachines.size() / 8; i++)
    {
        VMId_t vm = VM_Create(LINUX, POWER);
        VM_Attach(vm, powerMachines[i]);
        vms.push_back(vm);
        MachineAndVms *newMachine = new MachineAndVms(powerMachines[i], vm);
        activeMachinesAndVms[powerMachines[i]] = newMachine;
    }
    for (unsigned int i = 0; i < riscvMachines.size() / 8; i++)
    {
        VMId_t vm = VM_Create(LINUX, RISCV);
        VM_Attach(vm, riscvMachines[i]);
        vms.push_back(vm);
        MachineAndVms *newMachine = new MachineAndVms(riscvMachines[i], vm);
        activeMachinesAndVms[riscvMachines[i]] = newMachine;
    }
    for (unsigned int i = 0; i < x86Machines.size() / 8; i++)
    {
        VMId_t vm = VM_Create(LINUX, X86);
        VM_Attach(vm, x86Machines[i]);
        vms.push_back(vm);
        MachineAndVms *newMachine = new MachineAndVms(x86Machines[i], vm);
        activeMachinesAndVms[x86Machines[i]] = newMachine;
    }
    for (unsigned int i = 0; i < Machine_GetTotal(); i++)
    {
        if (activeMachinesAndVms.find(i) == activeMachinesAndVms.end())
        {
            Machine_SetState(MachineId_t(i), S1);
        }
    }
}
void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id)
{
    // Update your data structure. The VM now can receive new tasks
}
static const VMId_t NO_VM = INT_MAX;
// Safe get-or-create MachineAndVms for a machine id
static MachineAndVms *getOrCreateMachineInfo(MachineId_t m)
{
    auto it = activeMachinesAndVms.find(m);
    if (it != activeMachinesAndVms.end() && it->second != nullptr)
        return it->second;
    MachineAndVms *mi = new MachineAndVms(m, NO_VM);
    activeMachinesAndVms[m] = mi;
    return mi;
}
static void pickRandomMachine(const std::vector<MachineId_t> &vec, MachineId_t
                                                                       &outMachine)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, vec.size() - 1);
    outMachine = vec[dis(gen)];
}
void Scheduler::NewTask(Time_t now, TaskId_t task_id)
{
    CPUType_t required_cpu = RequiredCPUType(task_id);
    VMType_t vm_type = RequiredVMType(task_id);
    Priority_t priority;
    // SLA -> priority
    if (GetTaskInfo(task_id).required_sla == SLA0)
        priority = LOW_PRIORITY;
    else if (GetTaskInfo(task_id).required_sla == SLA1 ||
             GetTaskInfo(task_id).required_sla == SLA2)
        priority = MID_PRIORITY;
    else
        priority = HIGH_PRIORITY;
    bool assigned = false;
    MachineId_t nextMachine;
    MachineAndVms *nextMachineInfo = nullptr;
    vector<MachineId_t> cpuMachines;
    // Randomly choose machine by CPU type
    if (required_cpu == ARM)
        cpuMachines = armMachines;
    else if (required_cpu == POWER)
        cpuMachines = powerMachines;
    else if (required_cpu == X86)
        cpuMachines = x86Machines;
    else if (required_cpu == RISCV)
        cpuMachines = riscvMachines;
    bool goodMachine = false;
    pickRandomMachine(cpuMachines, nextMachine);
    // Check machine state and activeMachinesAndVms safely
    auto it = activeMachinesAndVms.find(nextMachine);
    if (it != activeMachinesAndVms.end())
        nextMachineInfo = it->second;

    // Machine sleeping? -> wake and add pending
    if (Machine_GetInfo(nextMachine).s_state != S0)
    {
        if (std::find(pendingMachines.begin(), pendingMachines.end(), nextMachine) == pendingMachines.end())
        {
            Machine_SetState(nextMachine, S0);
            pendingMachines.push_back(nextMachine);
        }
        if (!nextMachineInfo)
        {
            nextMachineInfo = new MachineAndVms(nextMachine, NO_VM);
            activeMachinesAndVms[nextMachine] = nextMachineInfo;
        }
        pending_tasks.push_back(new PendingTask(task_id, nextMachine));
        SimOutput("Scheduler::NewTask(): Waking machine " + to_string(nextMachine) + " and pending task " + to_string(task_id), 2);
        return;
    }
    // Machine is up -> check memory
    auto minfo = Machine_GetInfo(nextMachine);
    unsigned task_mem = GetTaskMemory(task_id);
    if (minfo.memory_used + task_mem > minfo.memory_size)
    {
        if (!nextMachineInfo)
        {
            nextMachineInfo = new MachineAndVms(nextMachine, NO_VM);
            activeMachinesAndVms[nextMachine] = nextMachineInfo;
        }
        pending_tasks.push_back(new PendingTask(task_id, nextMachine));
        SimOutput("Scheduler::NewTask(): Insufficient memory on machine " +
                      to_string(nextMachine) + " -> pending task " + to_string(task_id),
                  2);
        return;
    }
    // Ensure MachineAndVms exists
    if (!nextMachineInfo)
    {
        nextMachineInfo = new MachineAndVms(nextMachine, NO_VM);
        activeMachinesAndVms[nextMachine] = nextMachineInfo;
    }
    // Create VM if needed, then add task
    if (nextMachineInfo->vms[vm_type] == NO_VM)
    {
        VMId_t vm = VM_Create(vm_type, required_cpu);
        VM_Attach(vm, nextMachine);
        VM_AddTask(vm, task_id, priority);
        nextMachineInfo->vms[vm_type] = vm;
        task_to_machine[task_id] = nextMachine;
    }
    else
    {
        VM_AddTask(nextMachineInfo->vms[vm_type], task_id, priority);
        task_to_machine[task_id] = nextMachine;
    }
    SimOutput("Scheduler::NewTask(): Task " + to_string(task_id) + " assigned to Machine " + to_string(nextMachine), 1);
}

void Scheduler::addPendingTasks(Time_t now, MachineId_t machine)
{
    for (auto it = pending_tasks.begin(); it != pending_tasks.end();)
    {
        PendingTask *p = *it;
        MachineId_t neededMachine = p->machine;
        TaskId_t task = p->task;
        if (neededMachine == machine)
        {
            // Determine priority
            Priority_t priority;
            auto info = GetTaskInfo(task).required_sla;
            if (info == SLA0)
                priority = LOW_PRIORITY;
            else if (info == SLA1 || info == SLA2)
                priority = MID_PRIORITY;
            else
                priority = HIGH_PRIORITY;
            // Fetch or create MachineAndVms
            MachineAndVms *nextMachineInfo;
            nextMachineInfo = activeMachinesAndVms[neededMachine];
            if (nextMachineInfo == nullptr)
            {
                nextMachineInfo = new MachineAndVms(neededMachine, INT_MAX);
                activeMachinesAndVms[neededMachine] = nextMachineInfo;
            }
            // Attach the VM
            VMType_t type = RequiredVMType(task);
            if (nextMachineInfo->vms[type] == INT_MAX)
            {
                VMId_t vm = VM_Create(type, RequiredCPUType(task));
                VM_Attach(vm, neededMachine);
                VM_AddTask(vm, task, priority);
                nextMachineInfo->vms[type] = vm;
            }
            else
            {
                VM_AddTask(nextMachineInfo->vms[type], task, priority);
            }
            SimOutput("Scheduler::NewTask(): Task " + to_string(task) +
                          " on Machine " + to_string(neededMachine),
                      1);
            // CLEAN UP: delete pending task
            delete p;
            it = pending_tasks.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
void Scheduler::PeriodicCheck(Time_t now)
{
    for (auto it = pending_tasks.begin(); it != pending_tasks.end();)
    {
        PendingTask *p = *it;
        MachineId_t m = p->machine;
        TaskId_t t = p->task;
        auto info = Machine_GetInfo(m);
        // Machine must be awake
        if (info.s_state != S0)
        {
            ++it;
            continue;
        }
        unsigned mem_needed = GetTaskMemory(t);
        // Machine must have memory available
        if (info.memory_used + mem_needed > info.memory_size)
        {
            ++it;
            continue;
        }
        // Machine is fully ready → assign task
        MachineAndVms *mi = getOrCreateMachineInfo(m);
        VMType_t type = RequiredVMType(t);
        // Create VM if needed
        if (mi->vms[type] == NO_VM)
        {
            VMId_t vm = VM_Create(type, RequiredCPUType(t));
            VM_Attach(vm, m);
            mi->vms[type] = vm;
        }
        // Determine priority from SLA
        Priority_t priority;
        auto sla = GetTaskInfo(t).required_sla;
        if (sla == SLA0)
            priority = LOW_PRIORITY;
        else if (sla == SLA1 || sla == SLA2)
            priority = MID_PRIORITY;
        else
            priority = HIGH_PRIORITY;
        // Assign task
        VM_AddTask(mi->vms[type], t, priority);
        task_to_machine[t] = m;
        SimOutput("PeriodicCheck(): Assigned pending task " + to_string(t) + " to machine " + to_string(m), 2);
        // Remove task
        delete p;
        it = pending_tasks.erase(it);
    }
}
void Scheduler::Shutdown(Time_t time)
{
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for (auto &vm : vms)
    {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}
void Scheduler::TaskComplete(Time_t now, TaskId_t task_id)
{
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " complete at " + to_string(now), 4);
}
// Public interface below
static Scheduler Scheduler;
void InitScheduler()
{
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}
void HandleNewTask(Time_t time, TaskId_t task_id)
{
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}
void HandleTaskCompletion(Time_t time, TaskId_t task_id)
{
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}
void MemoryWarning(Time_t time, MachineId_t machine_id)
{
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}
void MigrationDone(Time_t time, VMId_t vm_id)
{
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
}
void SchedulerCheck(Time_t time)
{
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time),
              4);
    Scheduler.PeriodicCheck(time);
    static unsigned counts = 0;
    counts++;
    // if(counts == 10) {
    // migrating = true;
    // VM_Migrate(1, 9);
    // }
}
void SimulationComplete(Time_t time)
{
    // This function is called before the simulation terminates Add whatever youfeel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl; // SLA3 do not have SLA violation issues
    cout
        << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time) / 1000000 << " seconds"
         << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " +
                  to_string(time),
              4);
    Scheduler.Shutdown(time);
}
void SLAWarning(Time_t time, TaskId_t task_id)
{
}
void StateChangeComplete(Time_t time, MachineId_t machine_id)
{
    // Called in response to an earlier request to change the state of a machine
    auto it = std::find(pendingMachines.begin(), pendingMachines.end(),
                        machine_id);
    if (it != pendingMachines.end())
    {
        pendingMachines.erase(it);
    }
    if (Machine_GetInfo(machine_id).s_state == S0)
    {
        active_machines = active_machines + 1;
        Scheduler.addPendingTasks(time, machine_id);
    }
}
