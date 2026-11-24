# Three-client demonstration

Build and run tests first. Use disposable passwords and localhost.

1. Terminal 1: ./server_app. Terminals 2–4: ./client_app.
2. Client A: /register alice, enter a hidden password, then /login alice.
   Repeat for bob and carol in the other clients.
3. Alice types hello. All three see MSG general alice hello.
4. Alice: /msg bob private_demo. Only bob sees its content; alice gets acknowledgement.
5. Alice: /create study. Bob: /join study. Alice types room_demo.
   Alice/bob receive it; carol remains isolated in general.
6. Show /rooms, /users, /leave, /join general, and duplicate-room errors.
7. Alice logs out, tries an incorrect password, then logs in correctly.
   A simultaneous second alice login is rejected.
8. Show five threads with ps -T -p $(pgrep -n server_app).
   Show safe events with tail -f logs/server.log.
9. Bob /quit; listings/membership counts update.
10. Ctrl+C the server. Clients receive shutdown notice and exit.
    Restart and log in to demonstrate account persistence.

Use make test for isolated flood/capacity/queue/slow-buffer/shutdown checks and
make load for actual measurements. Avoid consuming the shared localhost auth
budget unnecessarily: initial burst 12, then one token every five seconds.
Never describe ordinary TCP as encrypted.
