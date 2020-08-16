# Distributed System Project
A simple solution to a distributed system mutual exclusion problem for Distributed Systems class

It uses a modified Lamport's Algorithm to solve the following problem:

There are tunnels between our world and another dimension, each with a limited capacity for people (all tunnels have the same capacity)
A number of people (processes) want to go to the other dimension using a tunnel, stay there for a while, go back, stay here for a while, go to tohe other dimension, 
and so on. Tunnels are one way only, that is once someone enters an empty tunnel in a certain direction only people going that direction can enter it, people
on the other side have to wait untill it is empty to enter.
