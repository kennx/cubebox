interface Props {
  title: string;
}

/** App 配置占位页：导航入口存在、内容后续版本提供 */
export default function PlaceholderCard({ title }: Props) {
  return (
    <section className="card placeholder">
      <span className="icon" aria-hidden="true">
        ⏳
      </span>
      <h2>{title}</h2>
      <p>该应用的配置将在后续版本提供</p>
      <span className="badge">即将上线</span>
    </section>
  );
}
