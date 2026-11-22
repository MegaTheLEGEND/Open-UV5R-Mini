# Third-party code

Code we did not write is allowed when its licence lets us use it, change it where needed, and redistribute it, in
this repo and in the images we build. That can be open source or proprietary (a chip vendor's SDK or libraries).

Before adding any:

1. Read its licence. No licence, an unclear one, or "internal use only" / "evaluation only" means no.
2. Check it fits with the other code in the image (for example, GPL code can't be linked with proprietary libraries).
3. Keep its copyright headers, licence file and NOTICE; put them next to the code.
4. Add a row below.

Never in this group: Baofeng's stock firmware or anything derived from it (no licence is granted; AGENTS.md rule 1).

| Code | Where in the repo | Licence | Source | Used for |
| --- | --- | --- | --- | --- |
| *(none yet)* | | | | |

Candidates, licence still to check: UV-K5 open firmware (DualTachyon, egzumer; Apache-2.0 per their repos), FreeRTOS
(MIT), JieLi's public AC69xx SDKs and libraries (Bluetooth, audio; licence to read before use).
