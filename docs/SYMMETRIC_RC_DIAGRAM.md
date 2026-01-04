```mermaid
graph TD
    subgraph Scope [Stack Scope / Owner]
        Root[Scope Root / Handle]
        Tether[Tether Token]
        style Root fill:#f9f,stroke:#333,stroke-width:2px
        style Tether fill:#ff9,stroke:#333,stroke-width:2px
    end

    subgraph Component [Cyclic Component (SCC)]
        Header[<b>Component Header</b><br/>Handle Count: 1<br/>Tether Count: 1]
        
        subgraph Objects [Member Objects]
            ObjA[Object A]
            ObjB[Object B]
        end
        
        style Header fill:#dfd,stroke:#333,stroke-width:2px
    end

    %% External References
    Root -->|Acquire Handle| Header
    Tether -.->|Scope Tether| Header

    %% Internal Structure
    Header -.->|Member List| ObjA
    Header -.->|Member List| ObjB
    
    ObjA -->|Internal Edge| ObjB
    ObjB -->|Internal Edge| ObjA

    %% Logic Note
    note[<b>Component Lifecycle</b><br/>1. <b>Handle</b>: Keeps component alive (ASAP managed).<br/>2. <b>Tether</b>: Zero-cost access lock (Scope managed).<br/>3. <b>Dismantle</b>: When Handle=0 AND Tether=0.<br/>4. <b>Internal Edges</b>: Ignored for liveness, cancelled on dismantle.]
    
    style note fill:#eee,stroke:#333,stroke-dasharray: 5 5

    %% Styling
    linkStyle 0 stroke:#f00,stroke-width:3px;
    linkStyle 1 stroke:#bb0,stroke-width:3px,stroke-dasharray: 5 5;
```

### ASCII Visualization

```text
       [ Scope / Stack ]
        │           ┆
 Handle │           ┆ Scope Tether
 (ASAP) │           ┆ (Zero-Cost)
        ▼           ▼
    ┌───────────────────────┐
    │   COMPONENT HEADER    │
    │ Handles: 1  Tether: 1 │
    └──────────┬────────────┘
               │
       ┌───────┴───────┐
       │ Member List   │
       ▼               ▼
  ┌──────────┐   ┌──────────┐
  │ Object A │<──>│ Object B │
  │ (SCC)    │   │ (SCC)    │
  └──────────┘   └──────────┘
```