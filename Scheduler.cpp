//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <cassert>
#include <queue>

static bool migrating = false;
static unsigned active_machines = 60;
// Keep track of how much memory is used on each machine
#include <map>
static map<VMId_t, MachineId_t> vm_to_machine;
static std::queue<TaskId_t> pending_tasks;
// have to make sure vm is to an accurate
void Scheduler::Init()
{
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    //
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    // create initial machines
    for (unsigned i = 0; i < active_machines; i++)
    {
        machines.push_back(MachineId_t(i));
        MachineInfo_t info = Machine_GetInfo(MachineId_t(i));

        VMId_t vm = VM_Create(LINUX, info.cpu);
        VM_Attach(vm, machines[i]);
        vms.push_back(vm);
        vm_to_machine[vm] = machines[i];
    }

    // add in all other machines to array
    for (unsigned i = active_machines; i < Machine_GetTotal(); i++)
    {
        machines.push_back(MachineId_t(i));
        Machine_SetState(MachineId_t(i), S1);
    }

    SimOutput("Scheduler::Init(): VM ids are " + to_string(vms[0]) + " and " + to_string(vms[1]), 3);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id)
{
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id)
{

    unsigned task_mem = GetTaskMemory(task_id);
    CPUType_t required_cpu = RequiredCPUType(task_id);
    VMType_t vm_type = RequiredVMType(task_id);
    Priority_t priority = MID_PRIORITY;
    bool assigned = false;

    for (unsigned int vm : vms)
    {

        MachineId_t m_id = vm_to_machine[vm];
        // need the right cpu, the right memory, the right VM type,
        if (required_cpu == Machine_GetCPUType(m_id) && VM_GetInfo(vm).vm_type == vm_type &&
            Machine_GetInfo(m_id).memory_used + task_mem <= Machine_GetInfo(m_id).memory_size)
        {
            VM_AddTask(vm, task_id, priority);
            SimOutput("Scheduler::NewTask(): Task " + to_string(task_id) +
                          " assigned to VM " + to_string(vm) +
                          " on Machine " + to_string(m_id),
                      1);
            assigned = true;
            break;
        }
    }

    if (!assigned)
    {
        for (unsigned int i = 0; i < Machine_GetTotal(); i++)
        {
            if (required_cpu == Machine_GetCPUType(i) &&
                Machine_GetInfo(i).memory_used + task_mem <= Machine_GetInfo(i).memory_size)
            {
                if (Machine_GetInfo(MachineId_t(i)).s_state != S0)
                {
                    Machine_SetState(MachineId_t(i), S0);
                    pending_tasks.push(task_id);
                }else {
                    VMId_t vm = VM_Create(vm_type, required_cpu);
                    VM_Attach(vm, MachineId_t(i));
                    vm_to_machine[vm] = MachineId_t(i);
                    VM_AddTask(vm, task_id, priority);
                    vms.push_back(vm);
                    SimOutput("Scheduler::NewTask(): Task " + to_string(task_id) +
                          " assigned to VM " + to_string(vm) +
                          " on Machine " + to_string(i),
                      1);
                    assigned = true;
                }
                break;
            }
        }
    }

    if (!assigned)
    {
        SimOutput("Scheduler::NewTask(): Could not assign Task " + to_string(task_id), 0);
        SLAWarning(now, task_id);
    }
}

//should create a struct to hold all active machine ids
//we cycle through to see which machine we can make a vm in
//once we find one we can make a vm in, we add and create the task


void Scheduler::PeriodicCheck(Time_t now)
{
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
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
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
}
void Scheduler::addPendingTasks(Time_t now, MachineId_t machine)
{
    //need to remove from vector
    for (int i = 0; i < pending_tasks.size(); i++)
    {
        TaskId_t id = pending_tasks.front();
        pending_tasks.pop();
        NewTask(now, id);
    }
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
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
    static unsigned counts = 0;
    counts++;
    // if(counts == 10) {
    //     migrating = true;
    //     VM_Migrate(1, 9);
    // }
}

void SimulationComplete(Time_t time)
{
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl; // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time) / 1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);

    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id)
{
}

void StateChangeComplete(Time_t time, MachineId_t machine_id)
{
    // Called in response to an earlier request to change the state of a machine
    if (Machine_GetInfo(machine_id).s_state == S0)
    {
        Scheduler.addPendingTasks(time, machine_id);
    }
}
