#include <stdio.h>

int main() {
    char name[50];
    int age;

    printf("Enter your name: ");
    scanf("%49s", name);

    printf("Enter your age: ");
    scanf("%d", &age);

    printf("\nHello %s!\n", name);
    printf("You are %d years old.\n", age);
    printf("Welcome to C Programming on Ubuntu!\n");

    return 0;
}
